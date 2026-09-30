// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio.h"
#include "test.h"

static uint32_t registers[128];
static uint8_t queue_page[4096] __attribute__((aligned(4096)));
static struct virtio_device device;
static struct virtio_queue queue;
static uint8_t payload[512];
void *coherent_page(unsigned slot) {
    return slot == 80 ? queue_page : NULL;
}
static void clear_registers(void) {
    for (unsigned i = 0; i < 128; i++)
        registers[i] = 0;
    registers[0] = 0x74726976;
    registers[1] = 2;
    registers[2] = VIRTIO_DEVICE_BLOCK;
    // A simple register fixture returns bit0 for both feature selectors;
    // high half bit0 supplies mandatory VERSION_1, low bit0 is ignored.
    registers[0x10 / 4] = 1;
    registers[0x34 / 4] = 32;
}
static void setup(void) {
    clear_registers();
    CHECK(virtio_device_init(&device, (uintptr_t)registers, 2, 0, 0), "modern device handshake");
    CHECK(virtio_queue_init(&queue, &device, 0, 80), "queue setup");
}
static void complete(uint16_t head, uint32_t length) {
    unsigned index = queue.used->index;
    queue.used->ring[index % queue.size].id = head;
    queue.used->ring[index % queue.size].length = length;
    queue.used->index = (uint16_t)(index + 1);
}
static void test_handshake(void) {
    setup();
    CHECK(device.features == VIRTIO_F_VERSION_1, "unsupported features never negotiated");
    CHECK(registers[0x70 / 4] == 11, "ACK|DRIVER|FEATURES_OK");
    CHECK(registers[0x38 / 4] == 32 && registers[0x44 / 4] == 1, "queue size and ready");
    CHECK(registers[0x80 / 4] == (uint32_t)(uintptr_t)queue.desc, "descriptor address low");
    CHECK(registers[0x84 / 4] == (uint32_t)((uintptr_t)queue.desc >> 32),
          "descriptor address high");
    CHECK(virtio_device_ready(&device), "DRIVER_OK");
    CHECK(registers[0x70 / 4] == 15, "all ready status bits");
    clear_registers();
    registers[1] = 1;
    CHECK(!virtio_device_init(&device, (uintptr_t)registers, 2, 0, 0), "legacy rejected");
    clear_registers();
    registers[0x10 / 4] = 0;
    CHECK(!virtio_device_init(&device, (uintptr_t)registers, 2, 0, 0), "VERSION_1 required");
    CHECK(registers[0x70 / 4] & 128, "failed feature handshake publishes FAILED");
    setup();
    registers[0x34 / 4] = 0;
    registers[0x44 / 4] = 0;
    CHECK(!virtio_queue_init(&queue, &device, 1, 80), "absent queue rejected");
    registers[0x34 / 4] = 7;
    CHECK(virtio_queue_init(&queue, &device, 1, 80) && queue.size == 4,
          "queue size is power of two");
}
static void test_chains(void) {
    setup();
    struct virtio_buffer buffers[3] = {{(uintptr_t)payload, 16, false},
                                       {(uintptr_t)(payload + 64), 256, true},
                                       {(uintptr_t)(payload + 400), 1, true}};
    uint16_t head, done;
    uint32_t length;
    registers[0x50 / 4] = 99;
    CHECK(virtio_queue_submit(&queue, buffers, 3, &head) == 0, "three descriptor request");
    CHECK(registers[0x50 / 4] == 99, "pre DRIVER_OK submission does not notify");
    CHECK(head == 0 && queue.desc[0].next == 1 && queue.desc[1].next == 2, "linked chain");
    CHECK(queue.desc[0].flags == 1 && queue.desc[1].flags == 3 && queue.desc[2].flags == 2,
          "device write direction");
    CHECK(queue.allocated == 7 && queue.pending == 1 && queue.outstanding == 1,
          "track owned chain");
    CHECK(virtio_device_ready(&device), "ready after posting");
    virtio_queue_notify(&queue);
    CHECK(registers[0x50 / 4] == 0, "explicit initial notification");
    CHECK(virtio_queue_poll(&queue, &done, &length) == 0, "no completion before device publishes");
    complete(head, 257);
    registers[0x60 / 4] = 1;
    CHECK(virtio_queue_poll(&queue, &done, &length) == 1 && done == head && length == 257,
          "completion cookie and length");
    CHECK(queue.allocated == 0 && queue.pending == 0 && queue.outstanding == 0,
          "release chain only on completion");
    CHECK(registers[0x64 / 4] == 1, "polling queue acknowledges interrupt");
    queue.avail->flags = 0;
    registers[0x64 / 4] = 0;
    CHECK(virtio_queue_submit(&queue, buffers, 3, &head) == 0, "repost chain");
    complete(head, 257);
    CHECK(virtio_queue_poll(&queue, &done, &length) == 1, "interrupt-driven completion");
    CHECK(registers[0x64 / 4] == 0, "IRQ-driven caller controls acknowledgement");
    buffers[0].length = 0;
    CHECK(virtio_queue_submit(&queue, buffers, 3, &head) == -1 && queue.allocated == 0,
          "invalid chain publishes nothing");
}
static void test_capacity_and_wrap(void) {
    setup();
    struct virtio_buffer buffer = {(uintptr_t)payload, 8, true};
    uint16_t heads[32], done;
    uint32_t length;
    for (unsigned i = 0; i < 32; i++) {
        CHECK(!virtio_queue_submit(&queue, &buffer, 1, heads + i), "fill queue slot%u", i);
    }
    CHECK(virtio_queue_submit(&queue, &buffer, 1, &done) < 0, "full queue rejects reuse");
    for (unsigned i = 0; i < 32; i++)
        complete(heads[31 - i], 8);
    for (unsigned i = 0; i < 32; i++) {
        CHECK(virtio_queue_poll(&queue, &done, &length) == 1 && done == heads[31 - i],
              "out-of-order completion%u", i);
    }
    CHECK(queue.allocated == 0 && queue.outstanding == 0, "queue empty after drain");
    queue.last_used = queue.used->index = queue.avail->index = 65535;
    CHECK(!virtio_queue_submit(&queue, &buffer, 1, &done) && queue.avail->index == 0,
          "available index wraps");
    complete(done, 8);
    CHECK(virtio_queue_poll(&queue, &done, &length) == 1 && queue.last_used == 0,
          "used index wraps");
}
static void test_corruption(void) {
    setup();
    uint16_t head, done;
    uint32_t length;
    struct virtio_buffer buffer = {(uintptr_t)payload, 8, true};
    CHECK(!virtio_queue_submit(&queue, &buffer, 1, &head), "submit before invalid ID");
    complete(32, 8);
    CHECK(virtio_queue_poll(&queue, &done, &length) < 0 && device.failed,
          "invalid used ID fails device");
    CHECK(queue.allocated == 1, "corrupt completion retains DMA ownership");
    CHECK(virtio_queue_submit(&queue, &buffer, 1, &head) < 0, "failed queue never reuses memory");
    setup();
    CHECK(!virtio_queue_submit(&queue, &buffer, 1, &head), "submit before excess completions");
    complete(head, 8);
    complete(head, 8);
    CHECK(virtio_queue_poll(&queue, &done, &length) < 0 && device.failed,
          "excess used advance rejected");
    setup();
    registers[0x70 / 4] |= 64;
    CHECK(virtio_queue_submit(&queue, &buffer, 1, &head) < 0 && device.failed,
          "DEVICE_NEEDS_RESET fails submission");
}
TEST_MAIN(test_handshake, test_chains, test_capacity_and_wrap, test_corruption)
