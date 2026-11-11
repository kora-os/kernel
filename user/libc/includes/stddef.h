// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stddef.h> (C11 7.19), over the compiler's predefined types.
#ifndef LIBC_STDDEF_H
#define LIBC_STDDEF_H

#define NULL ((void *)0)

typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef __SIZE_TYPE__ size_t;
#ifndef __cplusplus
typedef __WCHAR_TYPE__ wchar_t;
#endif

// The most strictly aligned scalar types.
typedef struct {
    long long __ll __attribute__((__aligned__(__alignof__(long long))));
    long double __ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif // LIBC_STDDEF_H
