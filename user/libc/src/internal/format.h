// SPDX-License-Identifier: GPL-3.0-or-later
//
// The printf engine shared by the snprintf family (src/stdio/snprintf.c) and,
// later, the FILE-based printf family. __format() parses `fmt`, converts the
// arguments and hands the output to `put` in pieces; it returns the number of
// characters produced, or -1 with errno set (EOVERFLOW past INT_MAX
// characters, EILSEQ for a wide character with no UTF-8 encoding). It uses a
// few dozen bytes of stack: programs' main threads have only 4 KB.
#ifndef LIBC_FORMAT_H
#define LIBC_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

typedef void (*format_put_fn)(void *ctx, const char *s, size_t n);

int __format(format_put_fn put, void *ctx, const char *fmt, va_list ap);

#endif // LIBC_FORMAT_H
