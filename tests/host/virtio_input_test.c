// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of the VirtIO input driver (src/drivers/virtio_input.c) against
// fake devices: a keyboard, a mouse (relative) and a tablet (absolute), each
// with registers, a config space that answers by its select and subsel (as a
// device does), an event queue of four buffers and an IRQ. Scenarios:
//   normal         a keyboard alone: keys in, events out, a bad completion
//   devices        a keyboard, a mouse and a tablet together
//   pointer        a device with no keys, movements or positions: not driven
//   bad-config     a keyboard whose config never holds still
//   queue-failure  a keyboard whose queues cannot be set up
#include "test.h"
#include "arch/irq.h"
#include "drivers/virtio.h"
#include "drivers/virtio_input.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "platform/virt.h"

int test_failures, test_checks;

enum kind { FAKE_KEYBOARD, FAKE_MOUSE, FAKE_TABLET, FAKE_NOTHING };

// A fake device: its registers (the config space at 0x100), what it is, and
// its event queue's buffers.
static struct fake {
    uint32_t registers[128];
    enum kind kind;
    unsigned irq;
    uint8_t selected[2];
    uint32_t generation;
    bool unstable;              // its config changes on every read
    struct virtio_avail available[2];
    uintptr_t posted[4];
    uint32_t allocated;
    uint16_t completed_head;
    uint32_t completed_length;
    int completion;
    irq_handler_t handler;
    void *ctx;
    unsigned submits, ready_calls, notifications;
    bool failed_queue_init;
    bool disconnected;
} fakes[3];
static unsigned fake_count;
static struct virt_platform platform;
static uint8_t pages[16][4096] __attribute__((aligned(4096)));
static unsigned queue_inits;
static struct input_event output[64];
static unsigned output_length;
static bool queue_fail;
static unsigned sources;

static struct fake *fake_at(uintptr_t base) {
    for (unsigned i = 0; i < fake_count; i++) {
        if ((uintptr_t)fakes[i].registers == base) {
            return &fakes[i];
        }
    }
    return NULL;
}

static struct fake *fake_of(const struct virtio_device *device) {
    return fake_at(device->base);
}

const struct virt_platform *virt_platform_get(void) { return &platform; }
unsigned irq_lines(void) { return 256; }
void irq_connect(unsigned irq, irq_handler_t callback, void *ctx) {
    struct fake *f = NULL;
    for (unsigned i = 0; i < fake_count; i++) {
        f = fakes[i].irq == irq ? &fakes[i] : f;
    }
    CHECK(f != NULL && ctx != NULL, "connect a discovered device's IRQ, with its context");
    if (f) {
        f->handler = callback;
        f->ctx = ctx;
    }
}
void irq_disconnect(unsigned irq) {
    for (unsigned i = 0; i < fake_count; i++) {
        if (fakes[i].irq == irq) {
            fakes[i].handler = NULL;
            fakes[i].disconnected = true;
        }
    }
}
unsigned input_source_register(const char *name) {
    (void)name;
    return 3 + sources++;
}
bool input_post(const struct input_event *event) {
    CHECK(output_length < sizeof(output) / sizeof(output[0]), "output test capture fits");
    if (output_length < sizeof(output) / sizeof(output[0])) output[output_length++] = *event;
    return true;
}
void *coherent_page(unsigned slot) {
    CHECK(slot >= VIRTIO_INPUT_QUEUE_SLOT + 2 && slot < VIRTIO_INPUT_QUEUE_SLOT + 3 * VIRTIO_INPUT_DEVICES &&
              (slot - VIRTIO_INPUT_QUEUE_SLOT) % 3 == 2,
          "receive events use a dedicated coherent page per device (%u)", slot);
    return pages[slot - VIRTIO_INPUT_QUEUE_SLOT];
}
bool virtio_device_init(struct virtio_device *device, uintptr_t base,
                        unsigned id, uint64_t required, uint64_t optional) {
    CHECK(id == 18 && !required && !optional, "negotiate modern input with no device feature bits");
    device->base = base;
    device->failed = false;
    return true;
}
void virtio_device_fail(struct virtio_device *device) { device->failed = true; }

// The config space: what the device says for the selection the driver just
// wrote. A new selection is answered, and the generation moves on, so the
// driver reads it again: what a device does when its config changes.
static void answer(struct fake *f) {
    uint8_t *config = (uint8_t *)f->registers + 0x100;
    uint8_t select = config[0], subsel = config[1];
    __builtin_memset(config + 2, 0, 0x100 - 2);
    uint8_t size = 0;
    uint8_t *bits = config + 8;
    if (select == 0x11 && subsel == 1 && f->kind == FAKE_KEYBOARD) {
        size = 4;
        bits[28 / 8] |= 1u << (28 % 8);  // Enter
        bits[30 / 8] |= 1u << (30 % 8);  // A
    } else if (select == 0x11 && subsel == 1 && f->kind != FAKE_NOTHING) {
        size = 35;                        // the buttons, from BTN_LEFT
        bits[0x110 / 8] |= 1u << (0x110 % 8);
    } else if (select == 0x11 && subsel == 2 && f->kind == FAKE_MOUSE) {
        size = 2;
        bits[0] = 0x03;                   // REL_X, REL_Y
        bits[1] = 0x01;                   // REL_WHEEL
    } else if (select == 0x11 && subsel == 3 && f->kind == FAKE_TABLET) {
        size = 1;
        bits[0] = 0x03;                   // ABS_X, ABS_Y
    } else if (select == 0x12 && subsel <= 1 && f->kind == FAKE_TABLET) {
        size = 20;                        // min 0, max 1000 on X, 2000 on Y
        uint32_t max = subsel == 0 ? 1000 : 2000;
        for (unsigned i = 0; i < 4; i++) {
            config[8 + 4 + i] = (uint8_t)(max >> (8 * i));
        }
    }
    config[2] = size;
}
uint32_t virtio_config_generation(struct virtio_device *device) {
    struct fake *f = fake_of(device);
    uint8_t *config = (uint8_t *)f->registers + 0x100;
    if (f->unstable) {
        return f->generation++;
    }
    if (config[0] != f->selected[0] || config[1] != f->selected[1]) {
        f->selected[0] = config[0];
        f->selected[1] = config[1];
        answer(f);
        f->generation++;
    }
    return f->generation;
}
bool virtio_queue_init(struct virtio_queue *queue, struct virtio_device *device,
                       unsigned number, unsigned slot) {
    struct fake *f = fake_of(device);
    unsigned n = (unsigned)(f - fakes);
    (void)n;
    CHECK(number < 2 && (slot - VIRTIO_INPUT_QUEUE_SLOT) % 3 == number,
          "dedicated event/status queue slots per device");
    queue_inits++;
    queue->device = device;
    queue->size = 4;
    queue->number = number;
    queue->avail = &f->available[number];
    queue->avail->flags = 1;
    if (queue_fail) {
        f->failed_queue_init = true;
    }
    return !queue_fail;
}
int virtio_queue_submit(struct virtio_queue *queue, const struct virtio_buffer *buffers,
                        unsigned count, uint16_t *head) {
    struct fake *f = fake_of(queue->device);
    CHECK(queue->number == 0 && count == 1 && buffers->length == 8 && buffers->device_writes,
          "event queue receives writable 8-byte buffers");
    CHECK(!queue->avail->flags, "enable input completion interrupts");
    for (unsigned i = 0; i < queue->size; i++) {
        if (!(f->allocated & (1u << i))) {
            f->allocated |= 1u << i;
            *head = (uint16_t)i;
            f->posted[i] = buffers->address;
            f->submits++;
            return 0;
        }
    }
    return -1;
}
bool virtio_device_ready(struct virtio_device *device) {
    struct fake *f = fake_of(device);
    CHECK(f->submits == 4 && f->handler, "populate entire receive queue and connect IRQ before DRIVER_OK");
    f->ready_calls++;
    return true;
}
void virtio_queue_notify(struct virtio_queue *queue) {
    struct fake *f = fake_of(queue->device);
    CHECK(queue->number == 0 && f->ready_calls, "notify after DRIVER_OK");
    f->notifications++;
}
int virtio_queue_poll(struct virtio_queue *queue, uint16_t *head, uint32_t *length) {
    struct fake *f = fake_of(queue->device);
    if (!f->completion) return 0;
    f->completion = 0;
    *head = f->completed_head;
    *length = f->completed_length;
    if (*head < 4) f->allocated &= ~(1u << *head);
    return 1;
}

// Device `f` completes one buffer with an evdev event, and interrupts.
static void event_on(struct fake *f, uint16_t type, uint16_t code, uint32_t value, uint32_t length) {
    uint8_t *buffer = (uint8_t *)f->posted[0];
    buffer[0] = (uint8_t)type; buffer[1] = (uint8_t)(type >> 8);
    buffer[2] = (uint8_t)code; buffer[3] = (uint8_t)(code >> 8);
    for (unsigned i = 0; i < 4; i++) buffer[4 + i] = (uint8_t)(value >> (i * 8));
    f->completed_head = 0;
    f->completed_length = length;
    f->completion = 1;
    f->registers[0x60 / 4] = 1;
    f->handler(f->ctx);
    CHECK(f->registers[0x64 / 4] == 1, "acknowledge input IRQ status");
}

static void event(uint16_t type, uint16_t code, uint32_t value, uint32_t length) {
    event_on(&fakes[0], type, code, value, length);
}

static bool posted_key(unsigned index, uint8_t type, uint8_t flags, uint16_t modifiers,
                       uint16_t keycode, uint32_t codepoint) {
    const struct input_event *e = &output[index];
    return index < output_length && e->type == type && e->flags == flags &&
           e->modifiers == modifiers && e->keycode == keycode && e->codepoint == codepoint &&
           e->source == 3;
}

static void add(enum kind kind, unsigned irq) {
    struct fake *f = &fakes[fake_count++];
    f->kind = kind;
    f->irq = irq;
    f->selected[0] = f->selected[1] = 0xFF;  // nothing selected yet
    platform.virtio[platform.virtio_count].base = (uintptr_t)f->registers;
    platform.virtio[platform.virtio_count].irq = irq;
    platform.virtio_count++;
}

static void keyboard_alone(void) {
    struct fake *k = &fakes[0];
    unsigned mice = 9, tablets = 9;
    CHECK(virtio_input_init() && k->handler && k->notifications == 1 && queue_inits == 2,
          "keyboard initialized");
    virtio_input_pointers(&mice, &tablets);
    CHECK(mice == 0 && tablets == 0, "and no pointer");
    CHECK(virtio_input_init() && k->submits == 4, "repeated init preserves active queues");
    event(1, 42, 1, 8); // Shift
    event(1, 30, 1, 8); // A
    event(1, 30, 2, 8); // repeat
    event(1, 30, 0, 8); // release
    event(1, 104, 1, 8); // ShiftPgUp
    event(2, 0, 5, 8); // pointer motion: not a key
    event(0, 0, 0, 8); // EV_SYN report
    CHECK(output_length == 5, "IRQ posts one event per key transition (%u)", output_length);
    CHECK(posted_key(0, INPUT_EVENT_KEY_DOWN, 0, INPUT_MOD_LEFT_SHIFT, HID_KEY_LEFT_CTRL + 1, 0),
          "IRQ posts the modifier key itself");
    CHECK(posted_key(1, INPUT_EVENT_KEY_DOWN, 0, INPUT_MOD_LEFT_SHIFT, HID_KEY_A, 'A') &&
          posted_key(2, INPUT_EVENT_KEY_DOWN, INPUT_FLAG_REPEAT, INPUT_MOD_LEFT_SHIFT, HID_KEY_A, 'A'),
          "IRQ posts key down and repeat with the modifier and the character");
    CHECK(posted_key(3, INPUT_EVENT_KEY_UP, 0, INPUT_MOD_LEFT_SHIFT, HID_KEY_A, 0),
          "IRQ posts the key going up");
    CHECK(posted_key(4, INPUT_EVENT_KEY_DOWN, 0, INPUT_MOD_LEFT_SHIFT, HID_KEY_PAGE_UP, 0),
          "IRQ posts Shift+PageUp as a key: scrolling is the consumer's business");
    CHECK(k->submits == 11 && k->allocated == 15, "IRQ reposts each consumed receive buffer");
    event(1, 30, 1, 7); // malformed truncated payload
    CHECK(!k->handler && k->disconnected && output_length == 5, "truncated completion stops keyboard safely");
    CHECK(!virtio_input_init(), "failed keyboard never reuses device-owned buffers");
}

static void three_devices(void) {
    struct fake *k = &fakes[0], *m = &fakes[1], *t = &fakes[2];
    unsigned mice = 0, tablets = 0;
    CHECK(virtio_input_init(), "a keyboard among three devices");
    virtio_input_pointers(&mice, &tablets);
    CHECK(mice == 1 && tablets == 1 && k->handler && m->handler && t->handler &&
              m->ctx != k->ctx && t->ctx != m->ctx && queue_inits == 6,
          "a mouse and a tablet too, each with its IRQ, context and queues");

    event_on(m, 2, 0, 5, 8);                 // REL_X +5
    event_on(m, 2, 1, (uint32_t)-3, 8);      // REL_Y -3
    event_on(m, 2, 0, 2, 8);                 // REL_X +2 more
    CHECK(output_length == 0, "a mouse's movement waits for its report");
    event_on(m, 0, 0, 0, 8);                 // SYN_REPORT
    CHECK(output_length == 1 && output[0].type == INPUT_EVENT_MOUSE && output[0].flags == 0 &&
              output[0].x == 7 && output[0].y == -3 && output[0].buttons == 0 &&
              output[0].wheel == 0 && output[0].source == 4,
          "one mouse event per report, the movements added up, from the mouse's source");
    event_on(m, 1, 0x110, 1, 8);             // BTN_LEFT down
    event_on(m, 1, 0x112, 1, 8);             // BTN_MIDDLE down
    event_on(m, 2, 8, (uint32_t)-1, 8);      // wheel towards the user
    event_on(m, 0, 0, 0, 8);
    CHECK(output_length == 2 && output[1].buttons == (INPUT_BUTTON_LEFT | INPUT_BUTTON_MIDDLE) &&
              output[1].x == 0 && output[1].y == 0 && output[1].wheel == -1,
          "buttons held and the wheel, with no movement");
    event_on(m, 1, 0x110, 0, 8);
    event_on(m, 0, 0, 0, 8);
    CHECK(output_length == 3 && output[2].buttons == INPUT_BUTTON_MIDDLE, "the left button released");
    event_on(m, 0, 0, 0, 8);
    CHECK(output_length == 3, "an empty report makes no event");

    event_on(t, 3, 0, 500, 8);               // ABS_X: half of 0..1000
    event_on(t, 3, 1, 2000, 8);              // ABS_Y: all of 0..2000
    event_on(t, 0, 0, 0, 8);
    CHECK(output_length == 4 && output[3].flags == INPUT_FLAG_ABSOLUTE &&
              output[3].x == INPUT_ABS_MAX / 2 && output[3].y == INPUT_ABS_MAX && output[3].source == 5,
          "a tablet's position, scaled to 0..INPUT_ABS_MAX (%d, %d)", output[3].x, output[3].y);
    event_on(t, 3, 0, 5000, 8);              // past the end
    event_on(t, 0, 0, 0, 8);
    CHECK(output_length == 5 && output[4].x == INPUT_ABS_MAX && output[4].y == INPUT_ABS_MAX,
          "clamped to the range, the other axis kept");

    event_on(k, 1, 30, 1, 8);
    CHECK(output_length == 6 && output[5].type == INPUT_EVENT_KEY_DOWN && output[5].source == 3,
          "the keyboard still types");
    event_on(m, 2, 0, 1, 7);                 // a bad completion stops the mouse alone
    CHECK(!m->handler && m->disconnected && k->handler && t->handler, "only the broken device stops");
    virtio_input_pointers(&mice, &tablets);
    CHECK(mice == 0 && tablets == 1, "and is no longer counted");
}

int main(int argc, char **argv) {
    const char *scenario = argc > 1 ? argv[1] : "normal";
    add(!strcmp(scenario, "pointer") ? FAKE_NOTHING : FAKE_KEYBOARD, 40);
    if (!strcmp(scenario, "devices")) {
        add(FAKE_MOUSE, 41);
        add(FAKE_TABLET, 42);
    }
    if (!strcmp(scenario, "bad-config")) fakes[0].unstable = true;
    if (!strcmp(scenario, "queue-failure")) queue_fail = true;
    if (!strcmp(scenario, "normal")) {
        keyboard_alone();
    } else if (!strcmp(scenario, "devices")) {
        three_devices();
    } else {
        bool initialized = virtio_input_init();
        CHECK(!initialized && !fakes[0].handler && !fakes[0].ready_calls,
              "unsupported device leaves no IRQ connected and no keyboard");
        CHECK(!output_length, "failed initialization emits no input");
        CHECK(!virtio_input_init(), "failed initialization cannot reuse DMA storage");
    }
    printf("virtio_input(%s): %d checks, %d failures\n", scenario, test_checks, test_failures);
    fflush(NULL);
    return test_failures != 0;
}
