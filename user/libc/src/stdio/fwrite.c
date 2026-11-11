// SPDX-License-Identifier: GPL-3.0-or-later
//
// Character and block output (C11 7.21.7, 7.21.8.2): fwrite, fputc, putc,
// putchar, fputs and puts, all through __fwrite_unlocked. A fully buffered
// stream writes when its buffer fills, a line buffered one also after each
// newline, an unbuffered one at once; data larger than the buffer goes out
// directly, without copying.

#include <errno.h>
#include <string.h>

#include "stdio_impl.h"
#include "syscall.h"

static size_t write_direct(FILE *f, const unsigned char *p, size_t n) {
    size_t done = 0;
    while (done < n) {
        long r = __syscall3(SYS_WRITE, f->fd, (long)(p + done), (long)(n - done));
        if (r <= 0) {
            f->flags |= F_ERR;
            errno = EIO;
            break;
        }
        done += (size_t)r;
    }
    return done;
}

size_t __fwrite_unlocked(const void *data, size_t n, FILE *f) {
    const unsigned char *p = data;
    if (!(f->flags & F_WRITE)) {
        f->flags |= F_ERR;
        errno = EBADF;
        return 0;
    }
    if (n == 0) {
        return 0;
    }
    if ((f->rlen > 0 || f->ungot != EOF) && __stdio_end_read(f) == EOF) {
        return 0;
    }
    __stdio_exit_hook = __stdio_exit;
    __stdio_setup_buffer(f);
    if (f->mode == _IONBF) {
        return write_direct(f, p, n);
    }
    size_t done = 0;
    if (n >= f->size) {
        // Too big to buffer: flush what is waiting, then write it directly.
        if (f->wlen > 0 && __fflush_unlocked(f) == EOF) {
            return 0;
        }
        return write_direct(f, p, n);
    }
    while (done < n) {
        size_t room = f->size - f->wlen;
        size_t k = n - done < room ? n - done : room;
        memcpy(f->buf + f->wlen, p + done, k);
        f->wlen += k;
        done += k;
        if (f->wlen == f->size && __fflush_unlocked(f) == EOF) {
            return done;  // accepted up to here; the stream reports the error
        }
    }
    if (f->mode == _IOLBF && memchr(p, '\n', n) != NULL && __fflush_unlocked(f) == EOF) {
        return n;  // accepted, but the stream now reports the error
    }
    return n;
}

size_t fwrite(const void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream) {
    if (size == 0 || nmemb == 0) {
        return 0;
    }
    size_t total = size * nmemb;
    if (total / size != nmemb) {
        errno = EOVERFLOW;
        return 0;
    }
    __lock(&stream->lock);
    size_t done = __fwrite_unlocked(ptr, total, stream);
    __unlock(&stream->lock);
    return done == total ? nmemb : done / size;
}

// Writes n bytes and reports whether this call failed: the error indicator
// may already be set from an earlier failure, which must not count.
static int write_failed(const void *data, size_t n, FILE *f) {
    unsigned earlier = f->flags & F_ERR;
    f->flags &= ~F_ERR;
    size_t done = __fwrite_unlocked(data, n, f);
    int failed = done != n || (f->flags & F_ERR);
    f->flags |= earlier;
    return failed;
}

int fputc(int c, FILE *stream) {
    unsigned char byte = (unsigned char)c;
    __lock(&stream->lock);
    int failed = write_failed(&byte, 1, stream);
    __unlock(&stream->lock);
    return failed ? EOF : byte;
}

int putc(int c, FILE *stream) {
    return fputc(c, stream);
}

int putchar(int c) {
    return fputc(c, stdout);
}

int fputs(const char *restrict s, FILE *restrict stream) {
    __lock(&stream->lock);
    int failed = write_failed(s, strlen(s), stream);
    __unlock(&stream->lock);
    return failed ? EOF : 0;
}

int puts(const char *s) {
    __lock(&stdout->lock);
    int failed = write_failed(s, strlen(s), stdout) || write_failed("\n", 1, stdout);
    __unlock(&stdout->lock);
    return failed ? EOF : 0;
}
