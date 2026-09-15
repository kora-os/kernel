#include "fs/blkdev.h"

// Symbols emitted by the generated fs_blob.S, which .incbin's the FAT32 ramdisk
// image into the kernel's read-only data.
extern const unsigned char _koraos_fs_start[];
extern const unsigned char _koraos_fs_end[];

// Local byte copy -- the freestanding kernel has no libc memcpy (task.c rolls
// its own the same way).
static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

// Ramdisk backend: sectors are served straight out of the embedded image. The
// image is read-only, so this only ever copies out.
static int ramdisk_read(blkdev_t *dev, uint32_t lba, uint32_t count, void *buf) {
    if (count == 0) {
        return 0;
    }
    uint64_t end = (uint64_t)lba + count;
    if (end > dev->sector_count) {
        return BLK_ERR_RANGE;
    }
    const unsigned char *base = dev->ctx;
    copy_bytes(buf, base + (uint64_t)lba * BLK_SECTOR_SIZE,
               (size_t)count * BLK_SECTOR_SIZE);
    return 0;
}

static blkdev_t ramdisk;
static blkdev_t *active;

void blkdev_init(void) {
    size_t bytes = (size_t)(_koraos_fs_end - _koraos_fs_start);
    ramdisk.read = ramdisk_read;
    ramdisk.sector_count = (uint32_t)(bytes / BLK_SECTOR_SIZE);
    ramdisk.ctx = (void *)_koraos_fs_start;
    active = &ramdisk;
}

int blk_read(uint32_t lba, uint32_t count, void *buf) {
    if (active == NULL) {
        return BLK_ERR_NODEV;
    }
    return active->read(active, lba, count, buf);
}

uint32_t blk_sector_count(void) {
    return active != NULL ? active->sector_count : 0;
}
