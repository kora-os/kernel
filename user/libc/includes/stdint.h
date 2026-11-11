// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdint.h> (C11 7.20), over the compiler's predefined types and limits, so
// the types are exactly the ones the compiler (and any host header) uses.
#ifndef LIBC_STDINT_H
#define LIBC_STDINT_H

typedef __INT8_TYPE__ int8_t;
typedef __UINT8_TYPE__ uint8_t;
typedef __INT16_TYPE__ int16_t;
typedef __UINT16_TYPE__ uint16_t;
typedef __INT32_TYPE__ int32_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __INT64_TYPE__ int64_t;
typedef __UINT64_TYPE__ uint64_t;

typedef __INT_LEAST8_TYPE__ int_least8_t;
typedef __UINT_LEAST8_TYPE__ uint_least8_t;
typedef __INT_LEAST16_TYPE__ int_least16_t;
typedef __UINT_LEAST16_TYPE__ uint_least16_t;
typedef __INT_LEAST32_TYPE__ int_least32_t;
typedef __UINT_LEAST32_TYPE__ uint_least32_t;
typedef __INT_LEAST64_TYPE__ int_least64_t;
typedef __UINT_LEAST64_TYPE__ uint_least64_t;

typedef __INT_FAST8_TYPE__ int_fast8_t;
typedef __UINT_FAST8_TYPE__ uint_fast8_t;
typedef __INT_FAST16_TYPE__ int_fast16_t;
typedef __UINT_FAST16_TYPE__ uint_fast16_t;
typedef __INT_FAST32_TYPE__ int_fast32_t;
typedef __UINT_FAST32_TYPE__ uint_fast32_t;
typedef __INT_FAST64_TYPE__ int_fast64_t;
typedef __UINT_FAST64_TYPE__ uint_fast64_t;

typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __INTMAX_TYPE__ intmax_t;
typedef __UINTMAX_TYPE__ uintmax_t;

#define INT8_MAX __INT8_MAX__
#define INT8_MIN (-INT8_MAX - 1)
#define UINT8_MAX __UINT8_MAX__
#define INT16_MAX __INT16_MAX__
#define INT16_MIN (-INT16_MAX - 1)
#define UINT16_MAX __UINT16_MAX__
#define INT32_MAX __INT32_MAX__
#define INT32_MIN (-INT32_MAX - 1)
#define UINT32_MAX __UINT32_MAX__
#define INT64_MAX __INT64_MAX__
#define INT64_MIN (-INT64_MAX - 1)
#define UINT64_MAX __UINT64_MAX__

#define INT_LEAST8_MAX __INT_LEAST8_MAX__
#define INT_LEAST8_MIN (-INT_LEAST8_MAX - 1)
#define UINT_LEAST8_MAX __UINT_LEAST8_MAX__
#define INT_LEAST16_MAX __INT_LEAST16_MAX__
#define INT_LEAST16_MIN (-INT_LEAST16_MAX - 1)
#define UINT_LEAST16_MAX __UINT_LEAST16_MAX__
#define INT_LEAST32_MAX __INT_LEAST32_MAX__
#define INT_LEAST32_MIN (-INT_LEAST32_MAX - 1)
#define UINT_LEAST32_MAX __UINT_LEAST32_MAX__
#define INT_LEAST64_MAX __INT_LEAST64_MAX__
#define INT_LEAST64_MIN (-INT_LEAST64_MAX - 1)
#define UINT_LEAST64_MAX __UINT_LEAST64_MAX__

#define INT_FAST8_MAX __INT_FAST8_MAX__
#define INT_FAST8_MIN (-INT_FAST8_MAX - 1)
#define UINT_FAST8_MAX __UINT_FAST8_MAX__
#define INT_FAST16_MAX __INT_FAST16_MAX__
#define INT_FAST16_MIN (-INT_FAST16_MAX - 1)
#define UINT_FAST16_MAX __UINT_FAST16_MAX__
#define INT_FAST32_MAX __INT_FAST32_MAX__
#define INT_FAST32_MIN (-INT_FAST32_MAX - 1)
#define UINT_FAST32_MAX __UINT_FAST32_MAX__
#define INT_FAST64_MAX __INT_FAST64_MAX__
#define INT_FAST64_MIN (-INT_FAST64_MAX - 1)
#define UINT_FAST64_MAX __UINT_FAST64_MAX__

#define INTPTR_MAX __INTPTR_MAX__
#define INTPTR_MIN (-INTPTR_MAX - 1)
#define UINTPTR_MAX __UINTPTR_MAX__
#define INTMAX_MAX __INTMAX_MAX__
#define INTMAX_MIN (-INTMAX_MAX - 1)
#define UINTMAX_MAX __UINTMAX_MAX__

#define PTRDIFF_MAX __PTRDIFF_MAX__
#define PTRDIFF_MIN (-PTRDIFF_MAX - 1)
#define SIZE_MAX __SIZE_MAX__
#define SIG_ATOMIC_MAX __SIG_ATOMIC_MAX__
#define SIG_ATOMIC_MIN (-SIG_ATOMIC_MAX - 1)
#define WCHAR_MAX __WCHAR_MAX__
#define WINT_MAX __WINT_MAX__
// Older compilers (clang before 19) do not predefine __WCHAR_MIN__ and
// __WINT_MIN__; derive them from the signedness.
#if defined(__WCHAR_MIN__)
#define WCHAR_MIN __WCHAR_MIN__
#elif defined(__WCHAR_UNSIGNED__)
#define WCHAR_MIN 0U
#else
#define WCHAR_MIN (-WCHAR_MAX - 1)
#endif
#if defined(__WINT_MIN__)
#define WINT_MIN __WINT_MIN__
#elif defined(__WINT_UNSIGNED__)
#define WINT_MIN 0U
#else
#define WINT_MIN (-WINT_MAX - 1)
#endif

// Integer constant macros: the literal gets the suffix of the matching type.
// GCC and recent clang predefine __INT64_C() and friends; older clang only
// the suffixes (__INT64_C_SUFFIX__). Types narrower than int need none.
#define _LIBC_C_JOIN(c, suffix) c##suffix
#define _LIBC_C(c, suffix) _LIBC_C_JOIN(c, suffix)
#define INT8_C(c) c
#define UINT8_C(c) c
#define INT16_C(c) c
#define UINT16_C(c) c
#define INT32_C(c) c
#if defined(__UINT32_C)
#define UINT32_C(c) __UINT32_C(c)
#else
#define UINT32_C(c) _LIBC_C(c, __UINT32_C_SUFFIX__)
#endif
#if defined(__INT64_C)
#define INT64_C(c) __INT64_C(c)
#define UINT64_C(c) __UINT64_C(c)
#define INTMAX_C(c) __INTMAX_C(c)
#define UINTMAX_C(c) __UINTMAX_C(c)
#else
#define INT64_C(c) _LIBC_C(c, __INT64_C_SUFFIX__)
#define UINT64_C(c) _LIBC_C(c, __UINT64_C_SUFFIX__)
#define INTMAX_C(c) _LIBC_C(c, __INTMAX_C_SUFFIX__)
#define UINTMAX_C(c) _LIBC_C(c, __UINTMAX_C_SUFFIX__)
#endif

#endif // LIBC_STDINT_H
