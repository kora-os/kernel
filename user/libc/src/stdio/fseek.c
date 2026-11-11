// SPDX-License-Identifier: GPL-3.0-or-later
//
// File positioning (C11 7.21.9): fseek, ftell, rewind, fgetpos, fsetpos.
// The kernel cannot seek past the end of a file, so neither can fseek
// (EINVAL); it cannot seek the console at all (ESPIPE).

#include <errno.h>

#include "stdio_impl.h"
#include "syscall.h"

// The logical position: where the fd is, minus what was read ahead and not
// consumed, plus what is buffered for writing.
static long tell_unlocked(FILE *f) {
    if (f->fd < KORA_FD_BASE) {
        errno = ESPIPE;
        return -1;
    }
    long pos = __syscall3(SYS_LSEEK, f->fd, 0, SEEK_CUR);
    if (pos < 0) {
        errno = EIO;
        return -1;
    }
    return pos - (long)(f->rlen - f->rpos) - (f->ungot != EOF ? 1 : 0) + (long)f->wlen;
}

int fseek(FILE *stream, long int offset, int whence) {
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    __lock(&stream->lock);
    int result = -1;
    if (stream->fd < KORA_FD_BASE) {
        errno = ESPIPE;
    } else {
        if (whence == SEEK_CUR) {
            long here = tell_unlocked(stream);
            if (here < 0) {
                goto out;
            }
            offset += here;
            whence = SEEK_SET;
        }
        if (stream->wlen > 0 && __fflush_unlocked(stream) == EOF) {
            goto out;
        }
        stream->rpos = stream->rlen = 0;
        stream->ungot = EOF;
        if (__syscall3(SYS_LSEEK, stream->fd, offset, whence) < 0) {
            errno = EINVAL;  // before the start or past the end
            goto out;
        }
        stream->flags &= ~F_EOF;
        result = 0;
    }
out:
    __unlock(&stream->lock);
    return result;
}

long int ftell(FILE *stream) {
    __lock(&stream->lock);
    long pos = tell_unlocked(stream);
    __unlock(&stream->lock);
    return pos;
}

void rewind(FILE *stream) {
    fseek(stream, 0, SEEK_SET);
    clearerr(stream);
}

int fgetpos(FILE *restrict stream, fpos_t *restrict pos) {
    long p = ftell(stream);
    if (p < 0) {
        return -1;
    }
    *pos = p;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
    return fseek(stream, *pos, SEEK_SET);
}
