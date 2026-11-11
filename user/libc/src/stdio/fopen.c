// SPDX-License-Identifier: GPL-3.0-or-later
//
// fopen, freopen and fclose (C11 7.21.5). Modes "r", "w", "a", "r+", "w+",
// "a+", each with an optional "b" (binary and text are the same here) and,
// for "w" modes, "x" (fail if the file exists). The kernel reports a failed
// open only as -1, so errno is the most likely reason, found with stat():
// ENOENT for a missing file, EEXIST for "x", EISDIR for a directory, EACCES
// otherwise (such as a read-only volume).

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "stdio_impl.h"
#include "syscall.h"

// The kernel's stat layout (src/sys/syscall.c, user/libk/koraos.h).
struct kora_stat {
    unsigned long size;
    int is_dir;
};

// open() flags for a mode string, or -1 if the mode is not valid.
static int mode_flags(const char *mode, unsigned *access) {
    int flags;
    switch (mode[0]) {
    case 'r': flags = KORA_O_RDONLY; *access = F_READ; break;
    case 'w': flags = KORA_O_WRONLY | KORA_O_CREAT | KORA_O_TRUNC; *access = F_WRITE; break;
    case 'a': flags = KORA_O_WRONLY | KORA_O_CREAT | KORA_O_APPEND; *access = F_WRITE; break;
    default: return -1;
    }
    for (const char *p = mode + 1; *p; p++) {
        if (*p == '+') {
            flags = (flags & ~(KORA_O_WRONLY | KORA_O_RDONLY)) | KORA_O_RDWR;
            *access = F_READ | F_WRITE;
        } else if (*p == 'x' && mode[0] == 'w') {
            flags |= KORA_O_EXCL;
        } else if (*p != 'b') {
            return -1;
        }
    }
    return flags;
}

static int open_error(const char *path, int flags) {
    struct kora_stat st;
    if (__syscall2(SYS_STAT, (long)path, (long)&st) < 0) {
        return ENOENT;  // also a missing parent directory when creating
    }
    if (flags & KORA_O_EXCL) {
        return EEXIST;
    }
    return st.is_dir ? EISDIR : EACCES;
}

static int open_fd(const char *path, const char *mode, unsigned *access) {
    int flags = mode_flags(mode, access);
    if (flags < 0) {
        errno = EINVAL;
        return -1;
    }
    long fd = __syscall2(SYS_OPEN, (long)path, flags);
    if (fd < 0) {
        errno = open_error(path, flags);
        return -1;
    }
    return (int)fd;
}

FILE *fopen(const char *restrict filename, const char *restrict mode) {
    FILE *f = malloc(sizeof *f);
    if (f == NULL) {
        return NULL;  // errno is ENOMEM
    }
    unsigned access;
    int fd = open_fd(filename, mode, &access);
    if (fd < 0) {
        free(f);
        return NULL;
    }
    memset(f, 0, sizeof *f);
    f->fd = fd;
    f->flags = access;
    f->mode = _IOFBF;
    f->ungot = EOF;
    f->lock = (lock_t)LOCK_INIT;
    __stdio_link(f);
    __stdio_exit_hook = __stdio_exit;  // so exit() closes it
    return f;
}

int __fclose_unlocked(FILE *f) {
    int result = 0;
    if (f->wlen > 0 && __fflush_unlocked(f) == EOF) {
        result = EOF;
    }
    if (f->fd >= KORA_FD_BASE && __syscall1(SYS_CLOSE, f->fd) < 0) {
        errno = EIO;
        result = EOF;
    }
    if (f->flags & F_OWNBUF) {
        free(f->buf);
    }
    f->buf = NULL;
    f->size = f->wlen = f->rpos = f->rlen = 0;
    f->ungot = EOF;
    f->flags = 0;  // no longer readable or writable
    f->fd = -1;
    return result;
}

static int is_standard(FILE *f) {
    return f == stdin || f == stdout || f == stderr;
}

int fclose(FILE *stream) {
    if (!is_standard(stream)) {
        __stdio_unlink(stream);
    }
    __lock(&stream->lock);
    int result = __fclose_unlocked(stream);
    __unlock(&stream->lock);
    if (!is_standard(stream)) {
        free(stream);
    }
    return result;
}

// Reopens `stream` on `filename` (the same file when it is NULL is not
// supported: EINVAL), keeping the FILE object, so stdin and stdout can be
// redirected to files.
FILE *freopen(const char *restrict filename, const char *restrict mode, FILE *restrict stream) {
    __lock(&stream->lock);
    __fclose_unlocked(stream);
    if (filename == NULL) {
        errno = EINVAL;
        __unlock(&stream->lock);
        return NULL;
    }
    unsigned access;
    int fd = open_fd(filename, mode, &access);
    if (fd >= 0) {
        stream->fd = fd;
        stream->flags = access;
        stream->mode = _IOFBF;
    }
    __unlock(&stream->lock);
    if (fd < 0) {
        if (!is_standard(stream)) {
            __stdio_unlink(stream);
            free(stream);
        }
        return NULL;
    }
    if (is_standard(stream)) {
        __stdio_exit_hook = __stdio_exit;
    }
    return stream;
}
