// SPDX-License-Identifier: GPL-3.0-or-later
//
// snprintf, vsnprintf, sprintf and vsprintf (C11 7.21.6): the printf engine
// (src/stdio/format.c) writing into a caller's buffer. The snprintf forms
// store at most n - 1 characters and a terminating NUL (nothing when n is 0)
// and return the length the whole output would have had.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "format.h"

struct buffer {
    char *data;
    size_t room;  // characters that still fit, the NUL excluded
    size_t used;
};

static void put_buffer(void *ctx, const char *s, size_t n) {
    struct buffer *b = ctx;
    size_t k = n < b->room ? n : b->room;
    if (k == 0) {
        return;  // full, or measuring only: data may be NULL, and NULL + 0 is UB
    }
    memcpy(b->data + b->used, s, k);
    b->used += k;
    b->room -= k;
}

int vsnprintf(char *restrict s, size_t n, const char *restrict format, __builtin_va_list arg) {
    struct buffer b = {s, n > 0 ? n - 1 : 0, 0};
    int result = __format(put_buffer, &b, format, arg);
    if (n > 0) {
        s[b.used] = '\0';
    }
    return result;
}

int snprintf(char *restrict s, size_t n, const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsnprintf(s, n, format, ap);
    va_end(ap);
    return result;
}

// No bound: the caller promises the buffer is large enough.
int vsprintf(char *restrict s, const char *restrict format, __builtin_va_list arg) {
    return vsnprintf(s, SIZE_MAX, format, arg);
}

int sprintf(char *restrict s, const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsprintf(s, format, ap);
    va_end(ap);
    return result;
}
