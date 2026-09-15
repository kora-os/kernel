#pragma once

#include "common.h"

// Block device layer: a linear array of fixed-size sectors underneath the
// filesystem. Today the only backend is an in-memory ramdisk (a FAT32 image
// embedded in the kernel image); a real SD/EMMC driver can register itself the
// same way later, so the FAT32 code above never has to change.

#define BLK_SECTOR_SIZE 512u

// Negative error codes, following the ELF_ERR_* convention.
#define BLK_ERR_NODEV (-1)  // no block device registered
#define BLK_ERR_RANGE (-2)  // requested sector range is outside the device

typedef struct blkdev {
    // Read `count` sectors starting at `lba` into `buf` (count * BLK_SECTOR_SIZE
    // bytes). Returns 0 on success or a negative BLK_ERR_* code.
    int (*read)(struct blkdev *dev, uint32_t lba, uint32_t count, void *buf);
    uint32_t sector_count;  // total number of sectors on the device
    void *ctx;              // backend-private data
} blkdev_t;

// Bring up the ramdisk block device from the embedded FAT32 image. Call once
// during kernel init, before mounting a filesystem.
void blkdev_init(void);

// Read from the active block device. Returns 0 or a negative BLK_ERR_* code.
int blk_read(uint32_t lba, uint32_t count, void *buf);

// Total sectors on the active device, or 0 if none is registered.
uint32_t blk_sector_count(void);
