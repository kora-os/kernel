// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input (C11 7.21.7, 7.21.8.1): fgetc, getc, getchar, fgets, fread and
// ungetc. A fully buffered stream reads ahead a buffer at a time; a line
// buffered or unbuffered one (stdin: the console is interactive) reads one
// byte per read, so input typed ahead stays queued for whoever reads next.
// Reading from stdin first flushes stdout, so a prompt shows up.
//
// The console hands over raw bytes: no echo, and Enter arrives as '\r'.
// Line editing is the program's job (libk's kgetline does it).

#include <errno.h>
#include <string.h>

#include "stdio_impl.h"
#include "syscall.h"

// Refills the read-ahead. 1 if there is data, 0 at end of file or error
// (with the indicator set).
static int fill(FILE *f) {
    if (!(f->flags & F_READ)) {
        f->flags |= F_ERR;
        errno = EBADF;
        return 0;
    }
    if (f->wlen > 0 && __fflush_unlocked(f) == EOF) {
        return 0;
    }
    if (f == stdin && stdout->mode == _IOLBF) {
        fflush(stdout);
    }
    __stdio_setup_buffer(f);
    unsigned char one;
    unsigned char *dest = f->buf != NULL ? f->buf : &one;
    size_t want = f->mode == _IOFBF && f->buf != NULL ? f->size : 1;
    long r = __syscall3(SYS_READ, f->fd, (long)dest, (long)want);
    if (r < 0) {
        f->flags |= F_ERR;
        errno = EIO;
        return 0;
    }
    if (r == 0) {
        f->flags |= F_EOF;
        return 0;
    }
    if (dest == &one) {
        f->ungot = one;  // unbuffered: hand the byte over through ungot
        return 1;
    }
    f->rpos = 0;
    f->rlen = (size_t)r;
    return 1;
}

static int getc_unlocked_(FILE *f) {
    if (f->ungot != EOF) {
        int c = f->ungot;
        f->ungot = EOF;
        return c;
    }
    if (f->rpos == f->rlen && !fill(f)) {
        return EOF;
    }
    if (f->ungot != EOF) {
        int c = f->ungot;
        f->ungot = EOF;
        return c;
    }
    return f->buf[f->rpos++];
}

int fgetc(FILE *stream) {
    __lock(&stream->lock);
    int c = getc_unlocked_(stream);
    __unlock(&stream->lock);
    return c;
}

int getc(FILE *stream) {
    return fgetc(stream);
}

int getchar(void) {
    return fgetc(stdin);
}

char *fgets(char *restrict s, int n, FILE *restrict stream) {
    if (n <= 0) {
        return NULL;
    }
    __lock(&stream->lock);
    unsigned earlier = stream->flags & F_ERR;  // only this call's errors count
    stream->flags &= ~F_ERR;
    int i = 0;
    while (i < n - 1) {
        int c = getc_unlocked_(stream);
        if (c == EOF) {
            break;
        }
        s[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    int error = (stream->flags & F_ERR) != 0;
    stream->flags |= earlier;
    __unlock(&stream->lock);
    if (i == 0 || error) {
        return NULL;  // end of file before any byte, or a read error
    }
    s[i] = '\0';
    return s;
}

size_t fread(void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream) {
    if (size == 0 || nmemb == 0) {
        return 0;
    }
    size_t total = size * nmemb;
    if (total / size != nmemb) {
        errno = EOVERFLOW;
        return 0;
    }
    unsigned char *p = ptr;
    size_t done = 0;
    __lock(&stream->lock);
    if (stream->ungot != EOF && done < total) {
        p[done++] = (unsigned char)stream->ungot;
        stream->ungot = EOF;
    }
    while (done < total) {
        size_t ahead = stream->rlen - stream->rpos;
        if (ahead > 0) {
            size_t k = total - done < ahead ? total - done : ahead;
            memcpy(p + done, stream->buf + stream->rpos, k);
            stream->rpos += k;
            done += k;
            continue;
        }
        if (stream->mode == _IOFBF && total - done >= stream->size && stream->size > 0 &&
            (stream->flags & F_READ) && stream->wlen == 0) {
            // Large reads go straight into the caller's memory.
            long r = __syscall3(SYS_READ, stream->fd, (long)(p + done), (long)(total - done));
            if (r <= 0) {
                stream->flags |= r < 0 ? F_ERR : F_EOF;
                if (r < 0) {
                    errno = EIO;
                }
                break;
            }
            done += (size_t)r;
            continue;
        }
        int c = getc_unlocked_(stream);
        if (c == EOF) {
            break;
        }
        p[done++] = (unsigned char)c;
    }
    __unlock(&stream->lock);
    return done / size;
}

int ungetc(int c, FILE *stream) {
    if (c == EOF) {
        return EOF;
    }
    __lock(&stream->lock);
    int result = EOF;
    if (stream->ungot == EOF && (stream->flags & F_READ)) {
        stream->ungot = (unsigned char)c;
        stream->flags &= ~F_EOF;
        result = (unsigned char)c;
    }
    __unlock(&stream->lock);
    return result;
}
