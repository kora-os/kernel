#pragma once

#include "common.h"

// Read-only FAT32 driver over the block device (include/fs/blkdev.h). Supports
// a single mounted volume, absolute paths, directory traversal with VFAT long
// filenames (LFN), file reads, and stat. No write support, no VFS -- just
// enough to read files (and later, load programs) off the embedded ramdisk.

#define FAT32_NAME_MAX 255

// Negative error codes, following the ELF_ERR_* convention.
#define FS_ERR_IO       (-1)  // block read failed
#define FS_ERR_NOFS     (-2)  // sector 0 is not a FAT32 BPB
#define FS_ERR_NOTFOUND (-3)  // path component does not exist
#define FS_ERR_NOTDIR   (-4)  // a non-final path component is not a directory
#define FS_ERR_ISDIR    (-5)  // open() targeted a directory (or read() on one)
#define FS_ERR_NOTFILE  (-6)  // opendir() targeted a file
#define FS_ERR_CORRUPT  (-7)  // structurally invalid (bad cluster chain)
#define FS_ERR_INVAL    (-8)  // bad argument (e.g. a relative path)

// An open handle: a byte cursor over a cluster chain. Used for both files and
// directories (directories ignore `size` and iterate to an end-of-dir marker).
typedef struct {
    uint32_t first_cluster;
    uint32_t size;  // file size in bytes; 0 and meaningless for directories
    uint32_t pos;   // byte offset of the cursor
    bool is_dir;
} fat32_file_t;

// One directory entry produced by fat32_readdir().
typedef struct {
    char name[FAT32_NAME_MAX + 1];  // decoded name (LFN if present, else 8.3)
    uint32_t size;
    uint32_t first_cluster;
    bool is_dir;
} fat32_dirent_t;

// Result of fat32_stat().
typedef struct {
    uint32_t size;
    bool is_dir;
} fat32_stat_t;

// Parse the BPB from sector 0 and cache the volume geometry. Returns 0 or a
// negative FS_ERR_* code. Call once, after blkdev_init().
int fat32_mount(void);

// Open a file by absolute path. Returns 0, or FS_ERR_ISDIR if the path names a
// directory, or FS_ERR_NOTFOUND / FS_ERR_NOTDIR / negative on other failures.
int fat32_open(const char *path, fat32_file_t *out);

// Read up to `len` bytes at the handle's cursor, advancing it. Returns the
// number of bytes read (0 at end of file), or a negative FS_ERR_* code.
long fat32_read(fat32_file_t *f, void *buf, uint32_t len);

// Open a directory by absolute path ("/" is the root). Returns 0 or negative.
int fat32_opendir(const char *path, fat32_file_t *out);

// Read the next entry from an open directory. Returns 1 and fills `out` for an
// entry, 0 at end of directory, or a negative FS_ERR_* code. Skips deleted
// entries, the volume label, and the "." / ".." links.
int fat32_readdir(fat32_file_t *dir, fat32_dirent_t *out);

// Stat an absolute path (file or directory). Returns 0 or negative.
int fat32_stat(const char *path, fat32_stat_t *out);
