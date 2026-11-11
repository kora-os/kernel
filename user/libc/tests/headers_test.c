// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the freestanding headers: <stddef.h>, <stdint.h>,
// <limits.h>, <stdbool.h>, <stdarg.h>, <float.h>, <stdalign.h> and
// <iso646.h>. Most checks are compile time, comparing each macro and type
// with what the compiler itself says. The same assertions are compiled for
// AArch64 by libc_header_check (see user/libc/CMakeLists.txt), so they hold on
// the target too, where char is unsigned and long double is binary128.

#include "test.h"

#include <float.h>
#include <iso646.h>
#include <limits.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "headers_static_checks.h"

struct layout {
    char c;
    long long ll;
    short s[3];
};

static void test_stddef(void) {
    CHECK(offsetof(struct layout, c) == 0, "offsetof first member");
    CHECK(offsetof(struct layout, ll) == alignof(long long), "offsetof after padding");
    CHECK(offsetof(struct layout, s[2]) == offsetof(struct layout, s) + 2 * sizeof(short),
          "offsetof array element");
    int a[10];
    ptrdiff_t d = &a[7] - &a[2];
    CHECK(d == 5, "ptrdiff_t difference");
    CHECK(NULL == (void *)0, "NULL");
    wchar_t w = L'A';
    CHECK(w == 65, "wchar_t holds L'A'");
}

static void test_limits_round_trip(void) {
    // Each maximum fits its type and wraps (unsigned) or is the edge (signed).
    unsigned char uc = UCHAR_MAX;
    CHECK(uc == UCHAR_MAX && (unsigned char)(uc + 1) == 0, "UCHAR_MAX wraps");
    unsigned short us = USHRT_MAX;
    CHECK((unsigned short)(us + 1) == 0, "USHRT_MAX wraps");
    unsigned int ui = UINT_MAX;
    CHECK(ui + 1u == 0, "UINT_MAX wraps");
    unsigned long ul = ULONG_MAX;
    CHECK(ul + 1ul == 0, "ULONG_MAX wraps");
    unsigned long long ull = ULLONG_MAX;
    CHECK(ull + 1ull == 0, "ULLONG_MAX wraps");
    char c = CHAR_MIN;
    CHECK(c == CHAR_MIN, "CHAR_MIN fits char");
    c = CHAR_MAX;
    CHECK(c == CHAR_MAX, "CHAR_MAX fits char");
    CHECK((char)-1 < 0 ? CHAR_MIN < 0 : CHAR_MIN == 0, "CHAR_MIN matches the signedness of char");
}

static void test_stdint_values(void) {
    CHECK(UINT64_MAX == 18446744073709551615ull, "UINT64_MAX");
    CHECK(INT64_MIN == -9223372036854775807ll - 1, "INT64_MIN");
    CHECK(INT8_MIN == -128 && INT8_MAX == 127 && UINT8_MAX == 255, "8-bit limits");
    CHECK(SIZE_MAX == (size_t)-1, "SIZE_MAX");
    CHECK(UINTPTR_MAX == (uintptr_t)-1, "UINTPTR_MAX");
    uintptr_t p = (uintptr_t)&p;
    CHECK((void *)p == (void *)&p, "uintptr_t round-trips a pointer");
    CHECK(UINT64_C(1) << 63 == 0x8000000000000000ull, "UINT64_C has a 64-bit type");
    CHECK(INTMAX_C(-1) < 0 && UINTMAX_C(1) > 0, "INTMAX_C, UINTMAX_C");
}

static void test_stdbool(void) {
    bool b = 5;
    CHECK(b == true && b == 1, "bool normalises to 1");
    b = 0;
    CHECK(b == false, "false");
    CHECK(__bool_true_false_are_defined == 1, "__bool_true_false_are_defined");
}

// Sums `count` arguments of alternating types: int, long long, double, char *.
static double sum_mixed(int count, ...) {
    va_list ap;
    va_start(ap, count);
    double total = 0;
    for (int i = 0; i < count; i++) {
        switch (i % 4) {
        case 0:
            total += va_arg(ap, int);
            break;
        case 1:
            total += (double)va_arg(ap, long long);
            break;
        case 2:
            total += va_arg(ap, double);
            break;
        default:
            total += (double)(va_arg(ap, char *)[0] - '0');
            break;
        }
    }
    va_end(ap);
    return total;
}

// Reads its arguments twice through va_copy.
static int sum_twice(int count, ...) {
    va_list ap, again;
    va_start(ap, count);
    va_copy(again, ap);
    int a = 0, b = 0;
    for (int i = 0; i < count; i++) {
        a += va_arg(ap, int);
    }
    for (int i = 0; i < count; i++) {
        b += va_arg(again, int);
    }
    va_end(again);
    va_end(ap);
    return a == b ? a : -1;
}

static void test_stdarg(void) {
    CHECK(sum_mixed(4, 1, 2ll, 0.5, "7") == 10.5, "va_arg over int, long long, double, char *");
    // More arguments than fit in registers, so some come from the stack.
    CHECK(sum_mixed(12, 1, 1ll, 1.0, "1", 1, 1ll, 1.0, "1", 1, 1ll, 1.0, "1") == 12.0,
          "va_arg past the register arguments");
    CHECK(sum_twice(10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55, "va_copy replays the arguments");
}

static void test_float_values(void) {
    CHECK(1.0 + DBL_EPSILON != 1.0 && 1.0 + DBL_EPSILON / 2 == 1.0, "DBL_EPSILON");
    CHECK(1.0f + FLT_EPSILON != 1.0f, "FLT_EPSILON");
    volatile double tiny = DBL_TRUE_MIN;
    CHECK(tiny > 0 && tiny / 2 == 0, "DBL_TRUE_MIN is the smallest subnormal");
    CHECK(FLT_RADIX == 2 && FLT_ROUNDS == 1, "radix and rounding");
}

static void test_alignment_and_iso646(void) {
    alignas(16) char buf[16];
    CHECK(((uintptr_t)buf & 15) == 0, "alignas(16)");
    CHECK(alignof(max_align_t) >= alignof(long double), "max_align_t is the strictest");
    int x = 6, y = 3;
    CHECK((x and y) && (x bitand y) == 2 && (x xor y) == 5 && (not 0), "iso646 spellings");
}

TEST_MAIN(test_stddef, test_limits_round_trip, test_stdint_values, test_stdbool, test_stdarg,
          test_float_values, test_alignment_and_iso646)
