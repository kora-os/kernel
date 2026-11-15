// SPDX-License-Identifier: GPL-3.0-or-later
//
// printf, fprintf, vprintf, vfprintf (C11 7.21.6) over the printf engine,
// with the stream locked for the whole call, and perror (C11 7.21.10.4).

#include <errno.h>
#include <string.h>

#include "format.h"
#include "stdio_impl.h"

static void put_stream(void *ctx, const char *s, size_t n) {
    __fwrite_unlocked(s, n, ctx);
}

// An unbuffered stream (stderr) would write each piece of the output as it
// is formatted: half a dozen writes for one message, and on a console shared
// with programs on other cores their output can land in between. So the
// stream borrows a small buffer for the call and the message goes out in one
// write (in a few, if it is longer than the buffer). Kept out of line: only
// this path pays for the buffer on the stack.
#define UNBUFFERED_LINE 160

__attribute__((noinline)) static int format_unbuffered(FILE *stream, const char *format,
                                                       __builtin_va_list arg) {
    unsigned char line[UNBUFFERED_LINE];
    unsigned char *buf = stream->buf;
    size_t size = stream->size;
    stream->buf = line;
    stream->size = sizeof(line);
    stream->mode = _IOFBF;
    int result = __format(put_stream, stream, format, arg);
    if (stream->wlen > 0) {
        __fflush_unlocked(stream);  // a failure sets the error indicator
    }
    stream->buf = buf;
    stream->size = size;
    stream->mode = _IONBF;
    return result;
}

int vfprintf(FILE *restrict stream, const char *restrict format, __builtin_va_list arg) {
    __lock(&stream->lock);
    unsigned had_error = stream->flags & F_ERR;
    stream->flags &= ~F_ERR;
    int result;
    if (stream->mode == _IONBF && (stream->flags & F_WRITE) && stream->wlen == 0 &&
        stream->rlen == 0 && stream->ungot == EOF) {
        result = format_unbuffered(stream, format, arg);
    } else {
        result = __format(put_stream, stream, format, arg);
    }
    if (stream->flags & F_ERR) {
        result = -1;  // an output error: errno is set by the write
    }
    stream->flags |= had_error;
    __unlock(&stream->lock);
    return result;
}

int fprintf(FILE *restrict stream, const char *restrict format, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    int result = vfprintf(stream, format, ap);
    __builtin_va_end(ap);
    return result;
}

int vprintf(const char *restrict format, __builtin_va_list arg) {
    return vfprintf(stdout, format, arg);
}

int printf(const char *restrict format, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, format);
    int result = vfprintf(stdout, format, ap);
    __builtin_va_end(ap);
    return result;
}

void perror(const char *s) {
    const char *message = strerror(errno);  // before any write can change errno
    if (s != NULL && *s != '\0') {
        fprintf(stderr, "%s: %s\n", s, message);
    } else {
        fprintf(stderr, "%s\n", message);
    }
}
