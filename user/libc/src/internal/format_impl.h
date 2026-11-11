// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internals of the printf engine shared by src/stdio/format.c (parsing and
// the integer, character and string conversions) and src/stdio/format_float.c
// (the floating-point conversions).
#ifndef LIBC_FORMAT_IMPL_H
#define LIBC_FORMAT_IMPL_H

#include <stddef.h>

#include "format.h"

#define FLAG_LEFT 1   // '-': pad on the right
#define FLAG_PLUS 2   // '+': always a sign on signed conversions
#define FLAG_SPACE 4  // ' ': a space where a plus sign would go
#define FLAG_ALT 8    // '#': 0/0x prefix for o, x, X; always a point for floats
#define FLAG_ZERO 16  // '0': pad numbers with zeros

#define NO_PRECISION (-1)

enum length { LEN_NONE, LEN_HH, LEN_H, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L };

struct out {
    format_put_fn put;
    void *ctx;
    size_t count;  // characters produced so far (never above INT_MAX)
    int error;     // errno value once something failed; nothing more is written
};

struct spec {
    int flags;
    int width;      // 0 when absent
    int precision;  // NO_PRECISION when absent
    enum length length;
    char conv;
};

void __format_emit(struct out *o, const char *s, size_t n);
void __format_pad(struct out *o, char c, size_t n);

// Spaces before (or, with '-', after) a field of `len` characters.
static inline size_t __format_field_padding(int width, size_t len) {
    return width > 0 && (size_t)width > len ? (size_t)width - len : 0;
}

// A e E f F g G a A of `value` (a long double argument arrives here already
// narrowed to double).
void __format_float(struct out *o, const struct spec *sp, double value);

#endif // LIBC_FORMAT_IMPL_H
