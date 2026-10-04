#pragma once

#include "common.h"
#include "fs/blkdev.h"

// FAT32 driver over the block device (include/fs/blkdev.h). Supports
// multiple mounted volumes, absolute paths, directory traversal with VFAT long
// filenames (LFN), existing-file data writes, truncate, and stat. No VFS.
//
// Names are returned as NUL-terminated UTF-8. On-disk LFN names are UTF-16, so
// the driver decodes UTF-16 (including surrogate pairs) to UTF-8 -- the right
// interface encoding for a byte-string / C-string ABI, and lossless versus the
// UTF-16 source. A consumer that cannot render a glyph (e.g. the kernel's ASCII
// framebuffer console) substitutes at draw time; the name itself stays intact.

// Maximum name length in UTF-8 bytes: an LFN holds up to 255 UTF-16 code units,
// which encode to at most 3 UTF-8 bytes each (surrogate pairs are 2 units -> 4
// bytes, i.e. fewer bytes per unit), so 255 * 3 bounds it.
#define FAT32_NAME_MAX 765

// Negative error codes, following the ELF_ERR_* convention.
#define FS_ERR_IO       (-1)  // block transport failed
#define FS_ERR_NOFS     (-2)  // sector 0 is not a FAT32 BPB
#define FS_ERR_NOTFOUND (-3)  // path component does not exist
#define FS_ERR_NOTDIR   (-4)  // a non-final path component is not a directory
#define FS_ERR_ISDIR    (-5)  // open() targeted a directory (or read() on one)
#define FS_ERR_NOTFILE  (-6)  // opendir() targeted a file
#define FS_ERR_CORRUPT  (-7)  // structurally invalid (bad cluster chain)
#define FS_ERR_INVAL    (-8)  // bad argument
#define FS_ERR_RO       (-9)  // medium or file is read-only
#define FS_ERR_NOSPC    (-10) // no free data clusters
#define FS_ERR_UNSUPPORTED (-11) // operation or durable flush unavailable
#define FS_ERR_BUSY     (-12) // live handles or directory pins

#define FAT32_O_RDONLY 0u
#define FAT32_O_WRONLY 1u
#define FAT32_O_RDWR   2u
#define FAT32_O_CREAT  0x100u // reserved for namespace operations
#define FAT32_O_TRUNC  0x200u
#define FAT32_O_APPEND 0x400u

typedef struct fat32_volume fat32_volume_t;
typedef struct fat32_inode fat32_inode_t;

// An open handle: a byte cursor over a cluster chain. Used for both files and
// directories (directories ignore `size` and iterate to an end-of-dir marker).
typedef struct {
    fat32_volume_t *volume;
    fat32_inode_t *inode; // shared metadata; handles own one reference
    uint32_t flags;
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
    uint8_t attributes;
    uint32_t entry_sector; // short entry location; root uses 0xffffffff
    uint16_t entry_offset;
} fat32_dirent_t;

// Result of fat32_stat().
typedef struct {
    uint32_t size;
    bool is_dir;
} fat32_stat_t;

// Mount a partition-relative device and allocate independent geometry/caches.
// Returns 0 or FS_ERR_*; *out is NULL on failure. Unmount only without handles.
int fat32_mount(blkdev_t *dev, fat32_volume_t **out);
int fat32_unmount(fat32_volume_t *volume); // busy or unsynced volumes remain alive
const char *fat32_label(const fat32_volume_t *volume);
bool fat32_is_read_only(const fat32_volume_t *volume);
int fat32_lookup(fat32_volume_t *volume, const char *path, fat32_dirent_t *out);

// Open a file by absolute path. Returns 0, or FS_ERR_ISDIR if the path names a
// directory, or FS_ERR_NOTFOUND / FS_ERR_NOTDIR / negative on other failures.
int fat32_open(fat32_volume_t *volume, const char *path, fat32_file_t *out);

// Read up to `len` bytes at the handle's cursor, advancing it. Returns the
// number of bytes read (0 at end of file), or a negative FS_ERR_* code.
long fat32_read(fat32_file_t *f, void *buf, uint32_t len);

// Open a directory by absolute path ("/" is the root). Returns 0 or negative.
int fat32_opendir(fat32_volume_t *volume, const char *path, fat32_file_t *out);

// Read the next entry from an open directory. Returns 1 and fills `out` for an
// entry, 0 at end of directory, or a negative FS_ERR_* code. Skips deleted
// entries, the volume label, and the "." / ".." links.
int fat32_readdir(fat32_file_t *dir, fat32_dirent_t *out);

// Stat an absolute path (file or directory). Returns 0 or negative.
int fat32_stat(fat32_volume_t *volume, const char *path, fat32_stat_t *out);

// Existing-file mutation APIs. No create/unlink or writable syscall exposure yet.
// Append chooses the shared EOF on every write. Seeking cannot create holes.
int fat32_open_flags(fat32_volume_t *volume, const char *path, uint32_t flags,
                     fat32_file_t *out);
void fat32_close(fat32_file_t *file);
uint32_t fat32_size(const fat32_file_t *file);
long fat32_seek(fat32_file_t *file, long offset, int whence);
long fat32_write(fat32_file_t *file, const void *buf, uint32_t len);
int fat32_truncate(fat32_file_t *file, uint32_t size);
int fat32_sync_volume(fat32_volume_t *volume);
// Mutation guards for namespace operations: handles and cwd/assign pins use
// the same reference-counted inode objects.
bool fat32_entry_busy(fat32_volume_t *volume, uint32_t sector, uint16_t offset);
bool fat32_directory_busy(fat32_volume_t *volume, uint32_t first_cluster);
