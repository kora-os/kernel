// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_blk.h"
#ifdef KORAOS_VIRT
#include "arch/cache.h"
#include "drivers/virtio.h"
#include "mini_uart.h"
#include "mm/coherent.h"
#include "mm/dma.h"
#include "platform/virt.h"

#define BLK_F_RO (1UL << 5)
#define BLK_F_FLUSH (1UL << 9)
#define BLK_REQUEST_IN 0u
#define BLK_REQUEST_OUT 1u
#define BLK_REQUEST_FLUSH 4u
#define BLK_STATUS_OK 0u
#define BLK_STATUS_UNSUPPORTED 2u
#define BLK_BOUNCE_SIZE 4096u
#define BLK_REQUEST_POLLS 10000000u
struct block_header {
    uint32_t type, reserved;
    uint64_t sector;
};
_Static_assert(sizeof(struct block_header) == 16, "VirtIO block request ABI");
static struct virtio_device disk;
static struct virtio_queue queue;
static blkdev_t block;
static volatile struct block_header *header;
static volatile uint8_t *status;
static uint8_t *bounce;
static bool initialized, present_disk, busy;

static void copy_bytes(void *dst, const void *src, size_t size) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < size; i++)
        d[i] = s[i];
}
static int request(uint32_t type, uint32_t sector, uint32_t bytes) {
    if (disk.failed)
        return BLK_ERR_IO;
    header->type = type;
    header->reserved = 0;
    header->sector = sector;
    *status = 0xff;
    struct virtio_buffer buffers[3] = {{(uintptr_t)header, sizeof(*header), false},
                                       {(uintptr_t)bounce, bytes, type == BLK_REQUEST_IN},
                                       {(uintptr_t)status, 1, true}};
    unsigned count = 3;
    if (type == BLK_REQUEST_FLUSH) {
        buffers[1] = buffers[2];
        count = 2;
    } else if (type == BLK_REQUEST_IN) {
        // Bounce memory is whole-cache-line aligned; discard dirty CPU lines
        // before ownership passes to the device, then invalidate on return.
        dcache_clean_invalidate(bounce, bytes);
    } else {
        dma_sync_for_device(bounce, bytes);
    }
    uint16_t submitted;
    if (virtio_queue_submit(&queue, buffers, count, &submitted))
        return BLK_ERR_IO;
    for (unsigned i = 0; i < BLK_REQUEST_POLLS; i++) {
        uint16_t completed;
        uint32_t length;
        int result = virtio_queue_poll(&queue, &completed, &length);
        if (result < 0)
            return BLK_ERR_IO;
        if (!result)
            continue;
        // Used length counts only writable descriptors: payload plus status
        // for a read, one status byte for writes and flushes.
        uint32_t expected = type == BLK_REQUEST_IN ? bytes + 1 : 1;
        if (completed != submitted || length > expected || length < 1 || *status == 0xff) {
            virtio_device_fail(&disk);
            return BLK_ERR_IO;
        }
        uint8_t result_status = *status;
        if (result_status == BLK_STATUS_UNSUPPORTED)
            return BLK_ERR_UNSUPPORTED;
        if (result_status != BLK_STATUS_OK)
            return BLK_ERR_IO;
        if (length != expected) {
            virtio_device_fail(&disk);
            return BLK_ERR_IO;
        }
        if (type == BLK_REQUEST_IN)
            dma_sync_for_cpu(bounce, bytes);
        return 0;
    }
    // DMA may still finish: retain bounce/request/ring forever; never reuse.
    virtio_device_fail(&disk);
    uart_puts("[virtio-blk] request timed out; disk disabled\r\n");
    return BLK_ERR_IO;
}
static int transfer(blkdev_t *dev, uint32_t lba, uint32_t count, void *buffer, bool write) {
    if ((uint64_t)lba + count > dev->sector_count)
        return BLK_ERR_RANGE;
    if (!count)
        return 0;
    if (!buffer)
        return BLK_ERR_INVALID;
    if (write && dev->read_only)
        return BLK_ERR_RO;
    if (__atomic_exchange_n(&busy, true, __ATOMIC_ACQUIRE))
        return BLK_ERR_BUSY;
    // A failed request may still own this bounce buffer. Check only after
    // acquiring serialization, before copying write data or touching DMA state.
    if (disk.failed) {
        __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
        return BLK_ERR_IO;
    }
    uint8_t *bytes = buffer;
    int result = 0;
    while (count) {
        uint32_t sectors =
            count < BLK_BOUNCE_SIZE / BLK_SECTOR_SIZE ? count : BLK_BOUNCE_SIZE / BLK_SECTOR_SIZE;
        uint32_t size = sectors * BLK_SECTOR_SIZE;
        if (write)
            copy_bytes(bounce, bytes, size);
        result = request(write ? BLK_REQUEST_OUT : BLK_REQUEST_IN, lba, size);
        if (result)
            break;
        if (!write)
            copy_bytes(bytes, bounce, size);
        bytes += size;
        lba += sectors;
        count -= sectors;
    }
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    return result;
}
static int disk_read(blkdev_t *dev, uint32_t lba, uint32_t count, void *buffer) {
    return transfer(dev, lba, count, buffer, false);
}
static int disk_write(blkdev_t *dev, uint32_t lba, uint32_t count, const void *buffer) {
    return transfer(dev, lba, count, (void *)buffer, true);
}
static int disk_flush(blkdev_t *dev) {
    if (dev->read_only)
        return BLK_ERR_RO;
    if (!(disk.features & BLK_F_FLUSH))
        return BLK_ERR_UNSUPPORTED;
    if (__atomic_exchange_n(&busy, true, __ATOMIC_ACQUIRE))
        return BLK_ERR_BUSY;
    int result = request(BLK_REQUEST_FLUSH, 0, 0);
    __atomic_store_n(&busy, false, __ATOMIC_RELEASE);
    return result;
}
blkdev_t *virtio_blk_init(bool *present) {
    if (initialized) {
        *present = present_disk;
        return present_disk && !disk.failed ? &block : NULL;
    }
    initialized = true;
    *present = false;
    const struct virt_platform *platform = virt_platform_get();
    uintptr_t base = 0;
    for (unsigned i = 0; i < platform->virtio_count; i++) {
        uintptr_t candidate = platform->virtio[i].base;
        if (*(volatile uint32_t *)candidate == 0x74726976u &&
            *(volatile uint32_t *)(candidate + 8) == VIRTIO_DEVICE_BLOCK) {
            base = candidate;
            break;
        }
    }
    if (!base)
        return NULL;
    *present = present_disk = true;
    if (!virtio_device_init(&disk, base, VIRTIO_DEVICE_BLOCK, 0, BLK_F_RO | BLK_F_FLUSH))
        return NULL;
    uint64_t capacity = 0;
    bool stable = false;
    for (unsigned i = 0; i < 16; i++) {
        uint32_t generation = virtio_config_generation(&disk);
        capacity = virtio_config_read32(&disk, 0);
        capacity |= (uint64_t)virtio_config_read32(&disk, 4) << 32;
        if (virtio_config_generation(&disk) == generation) {
            stable = true;
            break;
        }
    }
    if (!stable || !capacity || capacity > 0xffffffffu) {
        virtio_device_fail(&disk);
        return NULL;
    }
    header = coherent_page(VIRTIO_BLOCK_REQUEST_SLOT);
    if (!header) {
        virtio_device_fail(&disk);
        return NULL;
    }
    status = (volatile uint8_t *)header + 64;
    bounce = dma_alloc(BLK_BOUNCE_SIZE);
    if (!bounce || !virtio_queue_init(&queue, &disk, 0, VIRTIO_BLOCK_QUEUE_SLOT) ||
        queue.size < 4 || !virtio_device_ready(&disk)) {
        virtio_device_fail(&disk);
        return NULL;
    }
    block.read = disk_read;
    block.write = disk_write;
    block.flush = (disk.features & BLK_F_FLUSH) ? disk_flush : NULL;
    block.sector_count = capacity;
    block.read_only = !!(disk.features & BLK_F_RO);
    block.ctx = &disk;
    return &block;
}
#endif
