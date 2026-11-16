// SPDX-License-Identifier: GPL-3.0-or-later
//
// sscanf and vsscanf (C11 7.21.6.7, 7.21.6.14): the scanf engine reading a
// string. The end of the string is the end of the input. In a file of its
// own, so a program that only scans strings links no stream code.

#include <stdarg.h>
#include <stdio.h>

#include "scan.h"

struct string_input {
    struct scan_input base;  // first: the engine's pointer is a pointer to this
    const unsigned char *p;
};

static int string_get(struct scan_input *in) {
    struct string_input *s = (struct string_input *)in;
    return *s->p != '\0' ? *s->p++ : EOF;
}

static void string_unget(struct scan_input *in, int c) {
    (void)c;
    ((struct string_input *)in)->p--;
}

int vsscanf(const char *restrict s, const char *restrict format, va_list arg) {
    struct string_input in = {{string_get, string_unget}, (const unsigned char *)s};
    return __scan(&in.base, format, arg);
}

int sscanf(const char *restrict s, const char *restrict format, ...) {
    va_list ap;
    va_start(ap, format);
    int result = vsscanf(s, format, ap);
    va_end(ap);
    return result;
}
