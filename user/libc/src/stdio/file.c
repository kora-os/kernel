// SPDX-License-Identifier: GPL-3.0-or-later
//
// The standard streams, buffering and flushing (C11 7.21.3, 7.21.5): stdout
// is line buffered (it is the console), stderr unbuffered, and stdin line
// buffered, which for input means one byte per read (see fread.c).
// The first buffered write installs __stdio_exit as exit()'s hook, so every
// stream is flushed when the program exits, and programs that never use
// stdio do not link it.

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "stdio_impl.h"
#include "syscall.h"

static unsigned char stdout_buffer[BUFSIZ];
static unsigned char stdin_buffer[BUFSIZ];

static FILE standard[3] = {
    {0, F_READ, _IOLBF, stdin_buffer, sizeof stdin_buffer, 0, 0, 0, EOF, LOCK_INIT, NULL},
    {1, F_WRITE, _IOLBF, stdout_buffer, sizeof stdout_buffer, 0, 0, 0, EOF, LOCK_INIT, NULL},
    {2, F_WRITE, _IONBF, NULL, 0, 0, 0, 0, EOF, LOCK_INIT, NULL},
};

FILE *stdin = &standard[0];
FILE *stdout = &standard[1];
FILE *stderr = &standard[2];

static lock_t list_lock = LOCK_INIT;
static FILE *open_streams;

void __stdio_link(FILE *f) {
    __lock(&list_lock);
    f->next = open_streams;
    open_streams = f;
    __unlock(&list_lock);
}

void __stdio_unlink(FILE *f) {
    __lock(&list_lock);
    for (FILE **p = &open_streams; *p != NULL; p = &(*p)->next) {
        if (*p == f) {
            *p = f->next;
            break;
        }
    }
    __unlock(&list_lock);
}

void __stdio_each(void (*fn)(FILE *)) {
    for (int i = 0; i < 3; i++) {
        fn(&standard[i]);
    }
    __lock(&list_lock);
    for (FILE *f = open_streams; f != NULL; f = f->next) {
        fn(f);
    }
    __unlock(&list_lock);
}

void __stdio_setup_buffer(FILE *f) {
    if (f->buf == NULL && f->mode != _IONBF) {
        f->buf = malloc(BUFSIZ);
        if (f->buf == NULL) {
            f->mode = _IONBF;  // no memory: carry on unbuffered
            return;
        }
        f->size = BUFSIZ;
        f->flags |= F_OWNBUF;
    }
}

int __fflush_unlocked(FILE *f) {
    size_t done = 0;
    while (done < f->wlen) {
        long r = __syscall3(SYS_WRITE, f->fd, (long)(f->buf + done), (long)(f->wlen - done));
        if (r <= 0) {
            // The kernel gives no reason: report an I/O error and drop the
            // rest, so a broken stream does not retry forever.
            f->flags |= F_ERR;
            f->wlen = 0;
            errno = EIO;
            return EOF;
        }
        done += (size_t)r;
    }
    f->wlen = 0;
    return 0;
}

static void flush_one(FILE *f) {
    __lock(&f->lock);
    if (f->wlen > 0) {
        __fflush_unlocked(f);
    }
    __unlock(&f->lock);
}

// At exit: flush the standard streams, close everything fopen opened (so
// each file's directory entry is complete).
void __stdio_exit(void) {
    for (int i = 0; i < 3; i++) {
        flush_one(&standard[i]);
    }
    for (;;) {
        __lock(&list_lock);
        FILE *f = open_streams;
        __unlock(&list_lock);
        if (f == NULL) {
            return;
        }
        fclose(f);
    }
}

int __stdio_end_read(FILE *f) {
    size_t ahead = f->rlen - f->rpos + (f->ungot != EOF ? 1 : 0);
    f->rpos = f->rlen = 0;
    f->ungot = EOF;
    if (ahead > 0 && f->fd >= KORA_FD_BASE &&
        __syscall3(SYS_LSEEK, f->fd, -(long)ahead, SEEK_CUR) < 0) {
        f->flags |= F_ERR;
        errno = EIO;
        return EOF;
    }
    return 0;
}

int fflush(FILE *stream) {
    if (stream == NULL) {
        int result = 0;
        // Flush everything; report EOF if any stream failed.
        for (int i = 0; i < 3; i++) {
            __lock(&standard[i].lock);
            if (standard[i].wlen > 0 && __fflush_unlocked(&standard[i]) == EOF) {
                result = EOF;
            }
            __unlock(&standard[i].lock);
        }
        __lock(&list_lock);
        for (FILE *f = open_streams; f != NULL; f = f->next) {
            __lock(&f->lock);
            if (f->wlen > 0 && __fflush_unlocked(f) == EOF) {
                result = EOF;
            }
            __unlock(&f->lock);
        }
        __unlock(&list_lock);
        return result;
    }
    __lock(&stream->lock);
    int result = stream->wlen > 0 ? __fflush_unlocked(stream) : 0;
    __unlock(&stream->lock);
    return result;
}

int setvbuf(FILE *restrict stream, char *restrict buf, int mode, size_t size) {
    if (mode != _IOFBF && mode != _IOLBF && mode != _IONBF) {
        return -1;
    }
    __lock(&stream->lock);
    if (stream->wlen > 0) {
        __fflush_unlocked(stream);
    }
    if (stream->rlen > stream->rpos) {
        __stdio_end_read(stream);  // keep what was read ahead readable
    }
    if (buf != NULL && size > 0 && mode != _IONBF) {
        if (stream->flags & F_OWNBUF) {
            free(stream->buf);
        }
        stream->flags = (stream->flags & ~F_OWNBUF) | F_USERBUF;
        stream->buf = (unsigned char *)buf;
        stream->size = size;
    } else if (stream->flags & F_USERBUF) {
        // Stop using the caller's buffer; a new one is allocated on first use.
        stream->flags &= ~F_USERBUF;
        stream->buf = NULL;
        stream->size = 0;
    }
    stream->mode = mode;
    __unlock(&stream->lock);
    return 0;
}

void setbuf(FILE *restrict stream, char *restrict buf) {
    setvbuf(stream, buf, buf != NULL ? _IOFBF : _IONBF, BUFSIZ);
}

int ferror(FILE *stream) {
    __lock(&stream->lock);
    int r = (stream->flags & F_ERR) != 0;
    __unlock(&stream->lock);
    return r;
}

int feof(FILE *stream) {
    __lock(&stream->lock);
    int r = (stream->flags & F_EOF) != 0;
    __unlock(&stream->lock);
    return r;
}

void clearerr(FILE *stream) {
    __lock(&stream->lock);
    stream->flags &= ~(F_ERR | F_EOF);
    __unlock(&stream->lock);
}
