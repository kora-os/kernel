// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdarg.h> (C11 7.16), over the compiler's built-ins.
#ifndef LIBC_STDARG_H
#define LIBC_STDARG_H

typedef __builtin_va_list va_list;

#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_copy(dest, src) __builtin_va_copy(dest, src)
#define va_end(ap) __builtin_va_end(ap)

// The name glibc's own headers use for the same type, so host headers that
// expect it keep working when this header shadows the host's in unit tests.
#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
typedef __builtin_va_list __gnuc_va_list;
#endif

#endif // LIBC_STDARG_H
