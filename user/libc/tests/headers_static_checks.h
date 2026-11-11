// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compile-time checks of the freestanding headers against the compiler's own
// view of the types. Included by headers_test.c (host) and compiled for
// AArch64 by libc_header_check, so both sides agree. Uses only C11 features
// (_Static_assert, _Generic) and expects the headers to be included already.
#ifndef LIBC_HEADERS_STATIC_CHECKS_H
#define LIBC_HEADERS_STATIC_CHECKS_H

#define SAME_TYPE(expr, type) _Generic((expr), type: 1, default: 0)

// <stddef.h>
_Static_assert(SAME_TYPE(sizeof(int), size_t), "size_t is the type of sizeof");
_Static_assert(SAME_TYPE((char *)0 - (char *)0, ptrdiff_t), "ptrdiff_t is a pointer difference");
_Static_assert(SAME_TYPE(L'x', wchar_t) || sizeof(L'x') == sizeof(wchar_t), "wchar_t");
_Static_assert(_Alignof(max_align_t) >= _Alignof(long long), "max_align_t vs long long");
_Static_assert(_Alignof(max_align_t) >= _Alignof(long double), "max_align_t vs long double");

// <stdint.h>: exact widths, and the limits match the types.
_Static_assert(sizeof(int8_t) == 1 && sizeof(uint8_t) == 1, "8-bit types");
_Static_assert(sizeof(int16_t) == 2 && sizeof(uint16_t) == 2, "16-bit types");
_Static_assert(sizeof(int32_t) == 4 && sizeof(uint32_t) == 4, "32-bit types");
_Static_assert(sizeof(int64_t) == 8 && sizeof(uint64_t) == 8, "64-bit types");
_Static_assert((int8_t)-1 < 0 && (uint8_t)-1 > 0, "signedness of the 8-bit types");
_Static_assert(INT32_MAX == 2147483647 && INT32_MIN == -2147483647 - 1, "32-bit limits");
_Static_assert(UINT32_MAX == 4294967295u, "UINT32_MAX");
_Static_assert(INT64_MAX == 9223372036854775807ll, "INT64_MAX");
_Static_assert(sizeof(int_least8_t) >= 1 && sizeof(int_fast16_t) >= 2, "least and fast types");
_Static_assert(sizeof(intptr_t) == sizeof(void *) && sizeof(uintptr_t) == sizeof(void *),
               "pointer-sized integers");
_Static_assert(sizeof(intmax_t) >= sizeof(long long), "intmax_t is the widest");
_Static_assert(SIZE_MAX == (size_t)-1, "SIZE_MAX");
_Static_assert(PTRDIFF_MAX == (ptrdiff_t)(SIZE_MAX >> 1), "PTRDIFF_MAX");
_Static_assert(SAME_TYPE(UINT64_C(0), uint_least64_t), "UINT64_C type");
_Static_assert(SAME_TYPE(INT64_C(0), int_least64_t), "INT64_C type");
_Static_assert(SAME_TYPE(INT32_C(0), int), "INT32_C type");
_Static_assert(SAME_TYPE(INTMAX_C(0), intmax_t), "INTMAX_C type");
_Static_assert(WCHAR_MIN <= 0 && WCHAR_MAX > 0, "wchar_t range");

// <limits.h>
_Static_assert(CHAR_BIT == 8, "CHAR_BIT");
_Static_assert(SCHAR_MIN == -128 && SCHAR_MAX == 127 && UCHAR_MAX == 255, "char limits");
_Static_assert(((char)-1 < 0) ? (CHAR_MIN == SCHAR_MIN && CHAR_MAX == SCHAR_MAX)
                              : (CHAR_MIN == 0 && CHAR_MAX == UCHAR_MAX),
               "CHAR_MIN/CHAR_MAX follow the signedness of char");
_Static_assert(SHRT_MAX == 32767 && SHRT_MIN == -32768 && USHRT_MAX == 65535, "short limits");
_Static_assert(INT_MAX == INT32_MAX && INT_MIN == INT32_MIN && UINT_MAX == UINT32_MAX,
               "int limits");
_Static_assert(LONG_MAX == INT64_MAX && LONG_MIN == INT64_MIN && ULONG_MAX == UINT64_MAX,
               "long limits (LP64)");
_Static_assert(LLONG_MAX == INT64_MAX && ULLONG_MAX == UINT64_MAX, "long long limits");
_Static_assert(SAME_TYPE(UINT_MAX, unsigned int) && SAME_TYPE(ULONG_MAX, unsigned long) &&
                   SAME_TYPE(ULLONG_MAX, unsigned long long) && SAME_TYPE(LLONG_MIN, long long),
               "limit macro types");
_Static_assert(MB_LEN_MAX >= 4, "MB_LEN_MAX holds a UTF-8 character");

// <float.h>: IEEE single and double everywhere; long double depends on the
// platform but is at least double.
_Static_assert(FLT_MANT_DIG == 24 && DBL_MANT_DIG == 53, "IEEE float and double");
_Static_assert(FLT_MAX_EXP == 128 && DBL_MAX_EXP == 1024, "IEEE exponents");
_Static_assert(LDBL_MANT_DIG >= DBL_MANT_DIG, "long double is at least double");
_Static_assert(DBL_DECIMAL_DIG == 17 && FLT_DECIMAL_DIG == 9, "round-trip digit counts");

// <stdbool.h>, <stdalign.h>
_Static_assert(true == 1 && false == 0 && sizeof(bool) == sizeof(_Bool), "bool");
_Static_assert(__alignas_is_defined && __alignof_is_defined, "stdalign feature macros");

#endif // LIBC_HEADERS_STATIC_CHECKS_H
