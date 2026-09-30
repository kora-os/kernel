#pragma once

#include "common.h"

// Block device layer: a linear array of fixed-size sectors underneath the
// filesystem. The embedded ramdisk and VirtIO block device share this seam;
// filesystem code does not need to know the underlying transport.

#define BLK_SECTOR_SIZE 512u

// Negative error codes, following the ELF_ERR_* convention.
#define BLK_ERR_NODEV (-1)  // no block device registered
#define BLK_ERR_RANGE (-2)  // requested sector range is outside the device
#define BLK_ERR_IO (-3)
#define BLK_ERR_RO (-4)
#define BLK_ERR_UNSUPPORTED (-5)
#define BLK_ERR_INVALID (-6)
#define BLK_ERR_BUSY (-7)

typedef struct blkdev {
    // Read `count` sectors starting at `lba` into `buf` (count * BLK_SECTOR_SIZE
    // bytes). Returns 0 on success or a negative BLK_ERR_* code.
    int (*read)(struct blkdev *dev, uint32_t lba, uint32_t count, void *buf);
    uint32_t sector_count;  // total number of sectors on the device
    void *ctx;              // backend-private data
    int (*write)(struct blkdev *dev, uint32_t lba, uint32_t count, const void *buf);
    int (*flush)(struct blkdev *dev);
    bool read_only;
} blkdev_t;

// Select the target block backend before filesystem mount. With no attached
// VirtIO disk use the embedded ramdisk; a configured broken disk stays failed.
void blkdev_init(void);

// Read from the active block device. Returns 0 or a negative BLK_ERR_* code.
int blk_read(uint32_t lba, uint32_t count, void *buf);

// Raw device writes are optional; FAT32 remains read-only.
int blk_write(uint32_t lba, uint32_t count, const void *buf);
int blk_flush(void);
bool blk_is_read_only(void);

// Total sectors on the active device, or 0 if none is registered.
uint32_t blk_sector_count(void);
