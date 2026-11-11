// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdio.h> (C11 7.21), so far the formatted output to strings. FILE and the
// stream functions come next (see project_doc/libc-plan.md). The va_list
// parameters are spelled __builtin_va_list so this header does not define
// va_list itself, as the standard asks.
#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <stddef.h>

#include <_libc/cdefs.h>

#define EOF (-1)

int snprintf(char *restrict s, size_t n, const char *restrict format, ...) _LIBC_PRINTF(3, 4);
int sprintf(char *restrict s, const char *restrict format, ...) _LIBC_PRINTF(2, 3);
int vsnprintf(char *restrict s, size_t n, const char *restrict format, __builtin_va_list arg)
    _LIBC_PRINTF(3, 0);
int vsprintf(char *restrict s, const char *restrict format, __builtin_va_list arg)
    _LIBC_PRINTF(2, 0);

#endif // LIBC_STDIO_H
