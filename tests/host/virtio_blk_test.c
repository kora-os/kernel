// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio.h"
#include "drivers/virtio_blk.h"
#include "platform/virt.h"
#include "test.h"

static uint32_t registers[128] = {0x74726976, 2, 2};
static struct virt_platform platform;
static uint8_t request_page[4096] __attribute__((aligned(4096)));
static uint8_t dma_page[4096] __attribute__((aligned(4096)));
static uint8_t image[32 * 512];
static unsigned mode, requests, device_syncs, cpu_syncs, clean_invalidates, fails;
static uint32_t capacity_low = 32, capacity_high;
static uint32_t current_type, current_sector, current_bytes;
static uintptr_t current_data, current_status;
static int poll_mode;
static bool read_only, offer_flush = true, pending;
static blkdev_t *backend;

const struct virt_platform *virt_platform_get(void) {
    return &platform;
}
void uart_puts(const char *s) {
    (void)s;
}
void *coherent_page(unsigned slot) {
    return slot == 81 ? request_page : NULL;
}
void *dma_alloc(size_t size) {
    return size == 4096 ? dma_page : NULL;
}
void dma_sync_for_device(const void *ptr, size_t size) {
    CHECK(ptr == dma_page && size && size <= 4096, "valid clean range");
    device_syncs++;
}
void dma_sync_for_cpu(void *ptr, size_t size) {
    CHECK(ptr == dma_page && size && size <= 4096, "valid invalidate range");
    cpu_syncs++;
}
void dcache_clean_invalidate(const void *ptr, size_t size) {
    CHECK(ptr == dma_page && size && size <= 4096, "valid DMA preparation range");
    clean_invalidates++;
}
bool virtio_device_init(struct virtio_device *dev, uintptr_t base, unsigned id, uint64_t required,
                        uint64_t optional) {
    CHECK(base == (uintptr_t)registers && id == 2 && required == 0,
          "block transport initialization");
    dev->base = base;
    dev->features = VIRTIO_F_VERSION_1 | (read_only ? 1UL << 5 : 0) | (offer_flush ? 1UL << 9 : 0);
    CHECK((optional & ((1UL << 5) | (1UL << 9))) == ((1UL << 5) | (1UL << 9)),
          "RO and FLUSH offered negotiation");
    dev->failed = mode == 1;
    return !dev->failed;
}
bool virtio_queue_init(struct virtio_queue *q, struct virtio_device *dev, unsigned n,
                       unsigned slot) {
    CHECK(n == 0 && slot == 80, "block queue index/slot");
    q->device = dev;
    q->size = 32;
    return true;
}
bool virtio_device_ready(struct virtio_device *dev) {
    return !dev->failed;
}
void virtio_device_fail(struct virtio_device *dev) {
    dev->failed = true;
    fails++;
}
uint32_t virtio_config_read32(struct virtio_device *dev, unsigned offset) {
    (void)dev;
    return offset == 0 ? capacity_low : capacity_high;
}
uint32_t virtio_config_generation(struct virtio_device *dev) {
    (void)dev;
    return 0;
}
int virtio_queue_submit(struct virtio_queue *q, const struct virtio_buffer *buffers, unsigned count,
                        uint16_t *head) {
    (void)q;
    CHECK(!pending, "request ownership does not overlap");
    const uint32_t *header = (const uint32_t *)buffers[0].address;
    current_type = header[0];
    current_sector = (uint32_t)*(const uint64_t *)(header + 2);
    current_bytes = current_type == 4 ? 0 : buffers[1].length;
    current_data = buffers[1].address;
    current_status = buffers[count - 1].address;
    CHECK(buffers[0].length == 16 && !buffers[0].device_writes, "header is device readable");
    CHECK(buffers[count - 1].length == 1 && buffers[count - 1].device_writes,
          "status is device writable");
    CHECK(*(volatile uint8_t *)current_status == 0xff, "status reset before each transfer");
    CHECK(count == (current_type == 4 ? 2u : 3u), "flush vs data chain length");
    if (current_type != 4)
        CHECK(buffers[1].device_writes == (current_type == 0), "DMA direction");
    CHECK(current_sector + current_bytes / 512 <= 32, "bounded block request");
    // A preemption/reentrant read must report BUSY instead of corrupting bounce.
    uint8_t temporary[512];
    CHECK(backend->read(backend, 0, 1, temporary) == BLK_ERR_BUSY, "serialize reentrant callers");
    requests++;
    pending = true;
    *head = 7;
    return 0;
}
int virtio_queue_poll(struct virtio_queue *q, uint16_t *head, uint32_t *length) {
    (void)q;
    if (poll_mode == 1)
        return 0;
    CHECK(pending, "completion has matching request");
    pending = false;
    *head = 7;
    *length = current_type == 0 ? current_bytes + 1 : 1;
    *(uint8_t *)current_status = poll_mode == 2 ? 1 : poll_mode == 3 ? 2 : 0;
    if (poll_mode == 4)
        *length = 0;
    if (poll_mode == 5)
        *head = 6;
    if (poll_mode == 6)
        (*length)--;
    for (unsigned i = 0; i < current_bytes; i++) {
        unsigned index = current_sector * 512 + i;
        if (current_type == 0)
            ((uint8_t *)current_data)[i] = image[index];
        else
            image[index] = ((const uint8_t *)current_data)[i];
    }
    return 1;
}
static void configure(unsigned selected_mode) {
    mode = selected_mode;
    platform.virtio_count = 1;
    platform.virtio[0].base = (uintptr_t)registers;
}
static void test_backend(void) {
    bool present;
    configure(0);
    backend = virtio_blk_init(&present);
    CHECK(present && backend != NULL && backend->sector_count == 32, "disk backend selected");
    CHECK(backend == virtio_blk_init(&present), "initialization is idempotent");
    uint8_t buffer[10 * 512];
    for (unsigned i = 0; i < sizeof(image); i++)
        image[i] = (uint8_t)(i / 512);
    CHECK(backend->read(backend, 3, 10, buffer) == 0, "split bounce-page read");
    CHECK(requests == 2 && clean_invalidates == 2 && cpu_syncs == 2,
          "one cache handoff per bounded chunk");
    for (unsigned i = 0; i < sizeof(buffer); i++)
        CHECK(buffer[i] == 3 + i / 512, "read payload byte%u", i);
    CHECK(backend->read(backend, 31, 2, buffer) == BLK_ERR_RANGE, "read outside capacity rejected");
    CHECK(backend->read(backend, 0xffffffffu, 2, buffer) == BLK_ERR_RANGE,
          "LBA addition cannot wrap");
    CHECK(backend->read(backend, 0, 1, NULL) == BLK_ERR_INVALID, "NULL buffer rejected");
    CHECK(backend->read(backend, 0, 0, NULL) == 0, "empty transfer okay");
    for (unsigned i = 0; i < sizeof(buffer); i++)
        buffer[i] = 99;
    CHECK(backend->write(backend, 5, 10, buffer) == 0, "split bounce-page write");
    CHECK(device_syncs == 2, "write data cleaned for DMA");
    CHECK(image[5 * 512] == 99 && image[15 * 512 - 1] == 99 && image[15 * 512] == 15,
          "write range exact");
    CHECK(backend->flush(backend) == 0 && current_type == 4, "negotiated FLUSH request");
    poll_mode = 2;
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_IO, "device IO error propagated");
    poll_mode = 3;
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_UNSUPPORTED,
          "device unsupported propagated");
    poll_mode = 6;
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_IO && fails == 1,
          "short successful DMA read poisons device");
    unsigned before = requests;
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_IO && requests == before,
          "failed disk never reuses DMA");
}
static void test_scenario(char selected) {
    bool present;
    configure(selected == 'i' ? 1 : 0);
    if (selected == 'n')
        platform.virtio_count = 0;
    if (selected == 'c')
        capacity_high = 1;
    if (selected == 'z')
        capacity_low = 0;
    if (selected == 'r')
        read_only = true;
    if (selected == 'f')
        offer_flush = false;
    backend = virtio_blk_init(&present);
    if (selected == 'n') {
        CHECK(!backend && !present, "missing disk permits ramdisk fallback");
        return;
    }
    if (selected == 'i' || selected == 'c' || selected == 'z') {
        CHECK(!backend && present, "invalid configured disk cannot trigger fallback");
        CHECK(!virtio_blk_init(&present) && present,
              "failed initialization cannot later report a backend");
        return;
    }
    CHECK(backend && present, "initialized scenario disk");
    uint8_t buffer[512] = {0};
    if (selected == 'r') {
        CHECK(backend->read_only, "read-only feature honored");
        CHECK(backend->write(backend, 0, 1, buffer) == BLK_ERR_RO, "read-only writes rejected");
        CHECK(backend->flush(backend) == BLK_ERR_RO, "read-only flush rejected");
        CHECK(requests == 0, "RO requests never submitted");
        CHECK(!backend->read(backend, 0, 1, buffer), "read-only disk readable");
        return;
    }
    if (selected == 'f') {
        CHECK(backend->flush == NULL && requests == 0,
              "unnegotiated FLUSH capability is not advertised");
        return;
    }
    poll_mode = selected == 't' ? 1 : selected == 'l' ? 4 : 5;
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_IO && fails == 1,
          "timeout/malformed completion disables disk");
    CHECK(backend->read(backend, 0, 1, buffer) == BLK_ERR_IO && requests == 1,
          "disabled disk cannot reuse DMA buffer");
    uint8_t retained_data[sizeof(dma_page)], retained_request[sizeof(request_page)];
    for (unsigned i = 0; i < sizeof(dma_page); i++)
        retained_data[i] = dma_page[i];
    for (unsigned i = 0; i < sizeof(request_page); i++)
        retained_request[i] = request_page[i];
    for (unsigned i = 0; i < sizeof(buffer); i++)
        buffer[i] = 0xa5;
    unsigned cache_calls = device_syncs + cpu_syncs + clean_invalidates;
    CHECK(backend->write(backend, 1, 1, buffer) == BLK_ERR_IO && requests == 1,
          "write after failed request cannot submit or overwrite owned DMA");
    CHECK(backend->read(backend, 1, 1, buffer) == BLK_ERR_IO,
          "failed write releases serialization guard");
    CHECK(backend->flush(backend) == BLK_ERR_IO && requests == 1,
          "flush after failed request cannot reuse descriptor state");
    bool data_unchanged = true, request_unchanged = true;
    for (unsigned i = 0; i < sizeof(dma_page); i++)
        if (dma_page[i] != retained_data[i])
            data_unchanged = false;
    for (unsigned i = 0; i < sizeof(request_page); i++)
        if (request_page[i] != retained_request[i])
            request_unchanged = false;
    CHECK(data_unchanged && request_unchanged,
          "failed DMA payload/header/status retained unchanged");
    CHECK(cache_calls == device_syncs + cpu_syncs + clean_invalidates,
          "failed subsequent operations do not perform cache handoffs");
}
int test_failures, test_checks;
int main(int argc, char **argv) {
    if (argc > 1)
        test_scenario(argv[1][0]);
    else
        test_backend();
    printf("%s: %d checks, %d failures\n", __FILE__, test_checks, test_failures);
    fflush(NULL);
    return test_failures != 0;
}
