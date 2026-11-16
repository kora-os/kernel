// SPDX-License-Identifier: GPL-3.0-or-later
//
// fscanf, scanf, vfscanf and vscanf (C11 7.21.6): the scanf engine reading a
// stream, which stays locked for the whole call. The engine gives back at
// most the one character it read last, which is what ungetc guarantees, so
// the character after the last item is the next one the stream delivers.
//
// On stdin this reads the console as it is: raw, with no echo, and Enter
// arriving as '\r' (white space, so it ends a number like any other). Until
// the console has a cooked mode, scanf is of little use for typed input.

#include <stdarg.h>
#include <stdio.h>

#include "scan.h"
#include "stdio_impl.h"

struct stream_input {
    struct scan_input base;  // first: the engine's pointer is a pointer to this
    FILE *stream;
};

static int stream_get(struct scan_input *in) {
    return __stdio_getc(((struct stream_input *)in)->stream);
}

static void stream_unget(struct scan_input *in, int c) {
    __stdio_ungetc(c, ((struct stream_input *)in)->stream);
}

int vfscanf(FILE *restrict stream, const char *restrict format, va_list arg) {
    struct stream_input in = {{stream_get, stream_unget}, stream};
    __lock(&stream->lock);
    int result = __scan(&in.base, format, arg);
    __unlock(&stream->lock);
    return result;
}

int vscanf(const char *restrict format, va_list arg) {
    return vfscanf(stdin, format, arg);
}

int fscanf(FILE *restrict stream, const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfscanf(stream, format, ap);
    va_end(ap);
    return result;
}

int scanf(const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vfscanf(stdin, format, ap);
    va_end(ap);
    return result;
}
