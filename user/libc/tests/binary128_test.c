// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for __double_to_binary128() (src/internal/binary128.h), the
// integer-only widening strtold uses on AArch64. The host's long double is not
// binary128 (x87 on x86-64, double on Apple arm64), so the expected words are
// written out by hand from the IEEE 754 layouts.

#include "test.h"

#include "binary128.h"

static double from_bits(unsigned long long bits) {
    double d;
    __builtin_memcpy(&d, &bits, sizeof d);
    return d;
}

static void expect(double d, unsigned long long hi, unsigned long long lo, const char *what) {
    unsigned long long out[2];
    __double_to_binary128(d, out);
    CHECK(out[1] == hi && out[0] == lo, "%s: got %016llx %016llx, want %016llx %016llx", what,
          out[1], out[0], hi, lo);
}

static void test_normal_values(void) {
    expect(1.0, 0x3fff000000000000ull, 0, "1.0");
    expect(-2.0, 0xc000000000000000ull, 0, "-2.0");
    expect(1.5, 0x3fff800000000000ull, 0, "1.5");
    // 0.1 is 0x3fb999999999999a: the low fraction nibble moves to the low word.
    expect(0.1, 0x3ffb999999999999ull, 0xa000000000000000ull, "0.1");
    expect(from_bits(0x7fefffffffffffffull), 0x43feffffffffffffull, 0xf000000000000000ull,
           "DBL_MAX");
    expect(from_bits(0x0010000000000000ull), 0x3c01000000000000ull, 0, "DBL_MIN");
}

static void test_zeros_and_subnormals(void) {
    expect(0.0, 0, 0, "+0");
    expect(from_bits(0x8000000000000000ull), 0x8000000000000000ull, 0, "-0");
    // The smallest subnormal, 2^-1074, is a normal binary128 with no fraction.
    expect(from_bits(1), 0x3bcd000000000000ull, 0, "2^-1074");
    expect(from_bits(0x8000000000000001ull), 0xbbcd000000000000ull, 0, "-2^-1074");
    // The largest subnormal, (1 - 2^-52) * 2^-1022: 51 one bits after the leading one.
    expect(from_bits(0x000fffffffffffffull), 0x3c00ffffffffffffull, 0xe000000000000000ull,
           "largest subnormal");
    // 2^-1023 (only the top fraction bit set).
    expect(from_bits(0x0008000000000000ull), 0x3c00000000000000ull, 0, "2^-1023");
}

static void test_infinities_and_nans(void) {
    expect(from_bits(0x7ff0000000000000ull), 0x7fff000000000000ull, 0, "+inf");
    expect(from_bits(0xfff0000000000000ull), 0xffff000000000000ull, 0, "-inf");
    expect(from_bits(0x7ff8000000000000ull), 0x7fff800000000000ull, 0, "quiet NaN");
    // A signalling NaN keeps its payload, so it stays a NaN (fraction != 0).
    expect(from_bits(0x7ff0000000000001ull), 0x7fff000000000000ull, 0x1000000000000000ull,
           "signalling NaN payload");
}

TEST_MAIN(test_normal_values, test_zeros_and_subnormals, test_infinities_and_nans)
