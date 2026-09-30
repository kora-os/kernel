// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.h"
#include "arch/irq.h"
#include "drivers/virtio.h"
#include "drivers/virtio_input.h"
#include "platform/virt.h"

int test_failures, test_checks;
static uint32_t registers[128];
static struct virt_platform platform;
static struct virtio_avail available[2];
static uint8_t page[4096] __attribute__((aligned(4096)));
static uintptr_t posted[4];
static uint32_t allocated;
static uint16_t completed_head;
static uint32_t completed_length;
static int completion;
static unsigned queue_inits, submits, ready_calls, notifications, disconnected;
static irq_handler_t handler;
static char output[32];
static unsigned output_length;
static int scroll;
static bool queue_fail;

const struct virt_platform *virt_platform_get(void) { return &platform; }
unsigned irq_lines(void) { return 256; }
void irq_connect(unsigned irq, irq_handler_t callback, void *ctx) {
    CHECK(irq == 40 && !ctx, "connect discovered keyboard IRQ");
    handler = callback;
}
void irq_disconnect(unsigned irq) {
    CHECK(irq == 40, "disconnect keyboard on malformed completion");
    handler = NULL;
    disconnected++;
}
void tty_input_push(char c) {
    CHECK(output_length < sizeof(output), "output test capture fits");
    if (output_length < sizeof(output)) output[output_length++] = c;
}
void tty_scrollback(int halfpages) { scroll += halfpages; }
void *coherent_page(unsigned slot) {
    CHECK(slot == VIRTIO_INPUT_BUFFER_SLOT, "receive events use dedicated coherent page");
    return page;
}
bool virtio_device_init(struct virtio_device *device, uintptr_t base,
                        unsigned id, uint64_t required, uint64_t optional) {
    CHECK(id == 18 && !required && !optional, "negotiate modern input with no device feature bits");
    device->base = base;
    device->failed = false;
    return true;
}
void virtio_device_fail(struct virtio_device *device) { device->failed = true; }
uint32_t virtio_config_generation(struct virtio_device *device) {
    (void)device;
    return 0;
}
bool virtio_queue_init(struct virtio_queue *queue, struct virtio_device *device,
                       unsigned number, unsigned slot) {
    CHECK(number < 2 && slot == VIRTIO_INPUT_QUEUE_SLOT + number, "dedicated event/status queue slots");
    queue_inits++;
    queue->device = device;
    queue->size = 4;
    queue->number = number;
    queue->avail = &available[number];
    queue->avail->flags = 1;
    return !queue_fail;
}
int virtio_queue_submit(struct virtio_queue *queue, const struct virtio_buffer *buffers,
                        unsigned count, uint16_t *head) {
    CHECK(queue->number == 0 && count == 1 && buffers->length == 8 && buffers->device_writes,
          "event queue receives writable8-byte buffers");
    CHECK(!queue->avail->flags, "enable input completion interrupts");
    for (unsigned i = 0; i < queue->size; i++) {
        if (!(allocated & (1u << i))) {
            allocated |= 1u << i;
            *head = i;
            posted[i] = buffers->address;
            submits++;
            return 0;
        }
    }
    return -1;
}
bool virtio_device_ready(struct virtio_device *device) {
    (void)device;
    CHECK(submits == 4 && handler, "populate entire receive queue and connect IRQ before DRIVER_OK");
    ready_calls++;
    return true;
}
void virtio_queue_notify(struct virtio_queue *queue) {
    CHECK(queue->number == 0 && ready_calls, "notify after DRIVER_OK");
    notifications++;
}
int virtio_queue_poll(struct virtio_queue *queue, uint16_t *head, uint32_t *length) {
    (void)queue;
    if (!completion) return 0;
    completion = 0;
    *head = completed_head;
    *length = completed_length;
    if (*head < 4) allocated &= ~(1u << *head);
    return 1;
}
static void event(uint16_t type, uint16_t code, uint32_t value, uint32_t length) {
    uint8_t *buffer = (uint8_t *)posted[0];
    buffer[0] = type; buffer[1] = type >> 8;
    buffer[2] = code; buffer[3] = code >> 8;
    for (unsigned i = 0; i < 4; i++) buffer[4 + i] = value >> (i * 8);
    completed_head = 0;
    completed_length = length;
    completion = 1;
    registers[0x60 / 4] = 1;
    handler(NULL);
    CHECK(registers[0x64 / 4] == 1, "acknowledge input IRQ status");
}
int main(int argc, char **argv) {
    platform.virtio_count = 1;
    platform.virtio[0].base = (uintptr_t)registers;
    platform.virtio[0].irq = 40;
    uint8_t *config = (uint8_t *)registers + 0x100;
    config[2] = 4;
    config[11] = (1u << 4) | (1u << 6); // KEY_ENTER28, KEY_A30
    const char *scenario = argc > 1 ? argv[1] : "normal";
    if (!strcmp(scenario, "pointer")) config[11] = 0;
    if (!strcmp(scenario, "bad-config")) config[2] = 255;
    if (!strcmp(scenario, "queue-failure")) queue_fail = true;
    bool initialized = virtio_input_init();
    CHECK(config[0] == 0x11 && config[1] == 1, "query EV_KEY capabilities with select and subsel");
    if (strcmp(scenario, "normal")) {
        CHECK(!initialized && !handler && !ready_calls, "unsupported device leaves IRQ and TTY available");
        CHECK(!output_length, "failed initialization emits no input");
        CHECK(!virtio_input_init(), "failed initialization cannot reuse DMA storage");
    } else {
        CHECK(initialized && handler && notifications == 1 && queue_inits == 2, "keyboard initialized");
        CHECK(virtio_input_init() && submits == 4, "repeated init preserves active queues");
        event(1, 42, 1, 8); // Shift
        event(1, 30, 1, 8); // A
        event(1, 30, 2, 8); // repeat
        event(1, 30, 0, 8); // release
        event(1, 104, 1, 8); // ShiftPgUp
        CHECK(output_length == 2 && output[0] == 'A' && output[1] == 'A', "IRQ translates modifier and repeat");
        CHECK(scroll == 1, "IRQ applies scrollback request");
        CHECK(submits == 9 && allocated == 15, "IRQ reposts each consumed receive buffer");
        event(1, 30, 1, 7); // malformed truncated payload
        CHECK(!handler && disconnected == 1 && output_length == 2, "truncated completion stops keyboard safely");
        CHECK(!virtio_input_init(), "failed keyboard never reuses device-owned buffers");
    }
    printf("virtio_input(%s): %d checks, %d failures\n", scenario, test_checks, test_failures);
    fflush(NULL);
    return test_failures != 0;
}
