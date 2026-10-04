#pragma once

#include "fs/fat32.h"

#define FS_PATH_MAX 4096 // canonical volume-local path capacity, including NUL
#define FS_PREFIX_MAX 31 // capacity budget for future assign names, excluding colon
#define FS_QUALIFIED_PATH_MAX (FS_PATH_MAX + FS_PREFIX_MAX + 1)
#define FS_DEVICE_NAME_MAX 8

typedef struct fs_cwd {
    fat32_volume_t *volume;
    char path[FS_PATH_MAX]; // canonical absolute path within this volume
} fs_cwd_t;

typedef struct fs_volume_info {
    fat32_volume_t *volume;
    char device[FS_DEVICE_NAME_MAX]; // df0, df1, ...; no colon
    const char *label;
} fs_volume_info_t;

// Boot/test initialization only. Mounts every valid registered view. Boot must
// identify the exact preferred view; NULL or an invalid boot fails closed.
int fs_mount_registered(blkdev_t *boot);
void fs_namespace_reset(void); // only when no handles or tasks remain
unsigned fs_volume_count(void);
const fs_volume_info_t *fs_volume_get(unsigned index);
int fs_boot_cwd(fs_cwd_t *out);

// Every operation takes its caller's cwd explicitly. Resolution checks each
// component before processing a following '..'. Failure leaves cwd unchanged.
int fs_resolve(const fs_cwd_t *cwd, const char *path, fs_cwd_t *out,
               fat32_dirent_t *entry);
int fs_chdir(fs_cwd_t *cwd, const char *path);
int fs_getcwd(const fs_cwd_t *cwd, char *buf, size_t size); // 0 or FS_ERR_*
int fs_open(const fs_cwd_t *cwd, const char *path, fat32_file_t *out);
int fs_opendir(const fs_cwd_t *cwd, const char *path, fat32_file_t *out);
int fs_stat(const fs_cwd_t *cwd, const char *path, fat32_stat_t *out);
// Bare commands use the boot volume's /bin until assigns land in step 8.3.
int fs_program_open(const fs_cwd_t *cwd, const char *name, fat32_file_t *out);
