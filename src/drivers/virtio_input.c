// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_input.h"

#if defined(KORAOS_VIRT) && defined(KORAOS_VIRTIO_INPUT)
#include "arch/irq.h"
#include "drivers/virtio.h"
#include "input/input.h"
#include "mm/coherent.h"
#include "platform/virt.h"

#define INPUT_CFG_EV_BITS 0x11
#define INPUT_CFG_ABS_INFO 0x12
#define EV_KEY 1
#define EV_REL 2
#define EV_ABS 3
#define KEY_ENTER 28
#define KEY_A 30
#define REL_X 0
#define REL_Y 1
#define ABS_X 0
#define ABS_Y 1
#define INPUT_CONFIG_ATTEMPTS 8

// What the device writes into a receive buffer: a Linux evdev event.
struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
_Static_assert(sizeof(struct virtio_input_event) == 8, "VirtIO input event ABI");

enum input_kind { INPUT_NONE, INPUT_KEYBOARD, INPUT_MOUSE, INPUT_TABLET };

// One VirtIO input device: its queues, its receive buffers and what turns its
// evdev events into input events. Device i uses coherent slots
// VIRTIO_INPUT_QUEUE_SLOT + 3i (events), + 3i + 1 (status) and + 3i + 2 (its
// receive buffers).
struct input_device {
    struct virtio_device device;
    struct virtio_queue events;
    struct virtio_queue status;
    volatile struct virtio_input_event *buffers;
    uint8_t buffer_by_head[VIRTIO_QUEUE_MAX];
    enum input_kind kind;
    struct virtio_keymap_state keymap;
    struct virtio_pointer_state pointer;
    unsigned irq;
    uint8_t source;  // input_source_register()'s number for its events
    bool active;
};

static struct input_device devices[VIRTIO_INPUT_DEVICES];
static unsigned device_count;  // set up, active or stopped since
static bool attempted;

// input_lock (IRQ-safe) serializes queue setup against the interrupt handlers.
// Host tests run single-threaded without it.
#ifdef VIRTIO_INPUT_HOST_TEST
static uint64_t input_lock_take(void) { return 0; }
static void input_lock_drop(uint64_t flags) { (void)flags; }
#else
#include "arch/spinlock.h"
static struct spinlock input_lock = SPINLOCK_INIT("virtio input");
static uint64_t input_lock_take(void) { return spin_lock_irqsave(&input_lock); }
static void input_lock_drop(uint64_t flags) { spin_unlock_irqrestore(&input_lock, flags); }
#endif

static void barrier(void) {
#ifdef VIRTIO_INPUT_HOST_TEST
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    asm volatile("dmb osh" ::: "memory");
#endif
}

static uint32_t read_reg(struct input_device *d, unsigned offset) {
    return *(volatile uint32_t *)(d->device.base + offset);
}

static void write_reg(struct input_device *d, unsigned offset, uint32_t value) {
    *(volatile uint32_t *)(d->device.base + offset) = value;
}

// Read the device configuration selected by (select, subsel): its size, and
// up to `length` bytes of it. A config generation that changes meanwhile is
// read again. Returns false if it never held still.
static bool query(struct input_device *d, uint8_t select, uint8_t subsel, uint8_t *out,
                  unsigned length, uint8_t *size) {
    volatile uint8_t *config = (volatile uint8_t *)(d->device.base + 0x100);
    for (unsigned attempt = 0; attempt < INPUT_CONFIG_ATTEMPTS; attempt++) {
        uint32_t before = virtio_config_generation(&d->device);
        config[0] = select;
        config[1] = subsel;
        barrier();
        *size = config[2];
        for (unsigned i = 0; i < length; i++) {
            out[i] = config[8 + i];
        }
        if (before == virtio_config_generation(&d->device)) {
            return true;
        }
    }
    return false;
}

static bool has_bit(const uint8_t *bits, uint8_t size, unsigned bit) {
    return bit / 8 < size && (bits[bit / 8] & (1u << (bit % 8)));
}

// A keyboard has the alphabet and Enter; a mouse moves along X and Y; a
// tablet has positions on X and Y.
static enum input_kind classify(struct input_device *d) {
    uint8_t bits[16] = {0};
    uint8_t size;
    if (!query(d, INPUT_CFG_EV_BITS, EV_KEY, bits, sizeof(bits), &size) || size > 128) {
        return INPUT_NONE;
    }
    if (size >= 4 && has_bit(bits, size, KEY_A) && has_bit(bits, size, KEY_ENTER)) {
        return INPUT_KEYBOARD;
    }
    if (query(d, INPUT_CFG_EV_BITS, EV_REL, bits, 1, &size) && size >= 1 &&
        has_bit(bits, size, REL_X) && has_bit(bits, size, REL_Y)) {
        return INPUT_MOUSE;
    }
    if (query(d, INPUT_CFG_EV_BITS, EV_ABS, bits, 1, &size) && size >= 1 &&
        has_bit(bits, size, ABS_X) && has_bit(bits, size, ABS_Y)) {
        return INPUT_TABLET;
    }
    return INPUT_NONE;
}

// A tablet axis's range (struct virtio_input_absinfo: min, max, ...).
static bool axis_range(struct input_device *d, uint8_t axis, int32_t *min, int32_t *max) {
    uint8_t info[8];
    uint8_t size;
    if (!query(d, INPUT_CFG_ABS_INFO, axis, info, sizeof(info), &size) || size < 8) {
        return false;
    }
    *min = (int32_t)(info[0] | info[1] << 8 | info[2] << 16 | (uint32_t)info[3] << 24);
    *max = (int32_t)(info[4] | info[5] << 8 | info[6] << 16 | (uint32_t)info[7] << 24);
    return *max > *min;
}

static bool post_buffer(struct input_device *d, unsigned buffer_index) {
    struct virtio_buffer buffer = {
        .address = (uintptr_t)&d->buffers[buffer_index],
        .length = sizeof(struct virtio_input_event),
        .device_writes = true
    };
    uint16_t head;
    if (virtio_queue_submit(&d->events, &buffer, 1, &head) || head >= d->events.size) {
        return false;
    }
    d->buffer_by_head[head] = buffer_index;
    return true;
}

static void stop_device(struct input_device *d) {
    d->active = false;
    virtio_device_fail(&d->device);
    irq_disconnect(d->irq);
    // Queues and event buffers remain reserved: the device may still own them.
}

static void device_events(struct input_device *d);

static void device_irq(void *ctx) {
    uint64_t flags = input_lock_take();
    device_events(ctx);
    input_lock_drop(flags);
}

// One evdev event from the device, as an input event when it makes one.
static void translate(struct input_device *d, uint16_t type, uint16_t code, uint32_t value) {
    struct input_event event;
    bool made = d->kind == INPUT_KEYBOARD
                    ? virtio_keymap_event(&d->keymap, type, code, value, &event)
                    : virtio_pointer_event(&d->pointer, type, code, value, &event);
    if (made) {
        event.source = d->source;
        input_post(&event);
    }
}

static void device_events(struct input_device *d) {
    if (!d->active) {
        return;
    }
    uint32_t interrupts = read_reg(d, 0x60);
    if (interrupts) {
        write_reg(d, 0x64, interrupts);
        barrier();
    }
    if ((interrupts & 2) && classify(d) != d->kind) {
        stop_device(d);
        return;
    }
    // Work per IRQ is bounded; newly completed buffers reassert the IRQ since
    // only the status seen on entry was acknowledged. No allocation occurs.
    for (unsigned i = 0; i < d->events.size; i++) {
        uint16_t head;
        uint32_t length;
        int result = virtio_queue_poll(&d->events, &head, &length);
        if (!result) {
            return;
        }
        if (result < 0 || length != sizeof(struct virtio_input_event) || head >= d->events.size ||
            d->buffer_by_head[head] >= d->events.size) {
            stop_device(d);
            return;
        }
        unsigned index = d->buffer_by_head[head];
        volatile const struct virtio_input_event *event = &d->buffers[index];
        translate(d, event->type, event->code, event->value);
        if (!post_buffer(d, index)) {
            stop_device(d);
            return;
        }
    }
}

static const char *const source_names[] = {
    [INPUT_KEYBOARD] = "virtio-keyboard", [INPUT_MOUSE] = "virtio-mouse",
    [INPUT_TABLET] = "virtio-tablet"};

// Set up device `d` (slot number `n`), of kind `kind`, at `irq`.
static bool start_device(struct input_device *d, unsigned n, enum input_kind kind, unsigned irq) {
    unsigned slot = VIRTIO_INPUT_QUEUE_SLOT + 3 * n;
    d->kind = kind;
    d->irq = irq;
    if (kind == INPUT_TABLET) {
        int32_t min_x, max_x, min_y, max_y;
        if (!axis_range(d, ABS_X, &min_x, &max_x) || !axis_range(d, ABS_Y, &min_y, &max_y)) {
            return false;
        }
        virtio_pointer_init(&d->pointer, true, min_x, max_x, min_y, max_y);
    } else {
        virtio_pointer_init(&d->pointer, false, 0, 0, 0, 0);
    }
    if (!virtio_queue_init(&d->events, &d->device, 0, slot) ||
        !virtio_queue_init(&d->status, &d->device, 1, slot + 1)) {
        return false;
    }
    d->buffers = coherent_page(slot + 2);
    if (!d->buffers) {
        return false;
    }
    // The transport defaults to polling. Input instead requests used IRQs.
    d->events.avail->flags = 0;
    barrier();
    for (unsigned j = 0; j < VIRTIO_QUEUE_MAX; j++) {
        d->buffer_by_head[j] = 0xFF;
    }
    for (unsigned j = 0; j < d->events.size; j++) {
        if (!post_buffer(d, j)) {
            return false;
        }
    }
    irq_connect(d->irq, device_irq, d);
    if (!virtio_device_ready(&d->device)) {
        stop_device(d);
        return false;
    }
    d->source = (uint8_t)input_source_register(source_names[kind]);
    d->active = true;
    virtio_queue_notify(&d->events);
    return true;
}

// Each listed device that is an input device of a kind we drive, up to one
// keyboard and VIRTIO_INPUT_DEVICES in all. A device that fails to start
// keeps its slots: it may own the buffers posted to it.
static void initialize(void) {
    const struct virt_platform *platform = virt_platform_get();
    if (!platform) {
        return;
    }
    bool keyboard = false;
    for (unsigned i = 0; i < platform->virtio_count && device_count < VIRTIO_INPUT_DEVICES; i++) {
        const struct virt_mmio_device *candidate = &platform->virtio[i];
        struct input_device *d = &devices[device_count];
        if (candidate->irq >= irq_lines() ||
            !virtio_device_init(&d->device, candidate->base, VIRTIO_DEVICE_INPUT, 0, 0)) {
            continue;
        }
        enum input_kind kind = classify(d);
        if (kind == INPUT_NONE || (kind == INPUT_KEYBOARD && keyboard)) {
            virtio_device_fail(&d->device);
            continue;
        }
        unsigned n = device_count++;
        if (!start_device(d, n, kind, candidate->irq)) {
            if (!d->device.failed) {
                virtio_device_fail(&d->device);
            }
            continue;
        }
        keyboard |= kind == INPUT_KEYBOARD;
    }
}

bool virtio_input_init(void) {
    // Kernel bring-up already has systick IRQs enabled: serialize queue setup
    // against the interrupt handlers.
    uint64_t flags = input_lock_take();
    if (!attempted) {
        attempted = true;
        initialize();
    }
    bool keyboard = false;
    for (unsigned i = 0; i < device_count; i++) {
        keyboard |= devices[i].active && devices[i].kind == INPUT_KEYBOARD;
    }
    input_lock_drop(flags);
    return keyboard;
}

void virtio_input_pointers(unsigned *mice, unsigned *tablets) {
    unsigned m = 0, t = 0;
    for (unsigned i = 0; i < device_count; i++) {
        m += devices[i].active && devices[i].kind == INPUT_MOUSE;
        t += devices[i].active && devices[i].kind == INPUT_TABLET;
    }
    if (mice != NULL) {
        *mice = m;
    }
    if (tablets != NULL) {
        *tablets = t;
    }
}
#endif
