// SPDX-License-Identifier: GPL-3.0-or-later
//
// remove and rename (C11 7.21.4). remove deletes a file or an empty
// directory. As with fopen, the kernel only says -1, so errno is the most
// likely reason: ENOENT if the file is missing, otherwise EACCES for remove
// (a non-empty directory, a read-only volume) and EEXIST or EACCES for
// rename (the new name is taken, or the volume is read only).

#include <errno.h>

#include "stdio_impl.h"
#include "syscall.h"

struct kora_stat {
    unsigned long size;
    int is_dir;
};

int remove(const char *filename) {
    struct kora_stat st;
    if (__syscall2(SYS_STAT, (long)filename, (long)&st) < 0) {
        errno = ENOENT;
        return -1;
    }
    long r = __syscall1(st.is_dir ? SYS_RMDIR : SYS_UNLINK, (long)filename);
    if (r < 0) {
        errno = st.is_dir ? ENOTEMPTY : EACCES;
        return -1;
    }
    return 0;
}

int rename(const char *old, const char *new) {
    if (__syscall2(SYS_RENAME, (long)old, (long)new) == 0) {
        return 0;
    }
    struct kora_stat st;
    if (__syscall2(SYS_STAT, (long)old, (long)&st) < 0) {
        errno = ENOENT;
    } else if (__syscall2(SYS_STAT, (long)new, (long)&st) == 0) {
        errno = EEXIST;
    } else {
        errno = EACCES;
    }
    return -1;
}
