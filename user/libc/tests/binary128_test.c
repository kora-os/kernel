// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for __double_to_binary128() and __binary128_to_double()
// (src/internal/binary128.h), the integer-only conversions strtold and the
// printf engine use for long double on AArch64. The host's long double is not
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

static unsigned long long to_bits(double d) {
    unsigned long long b;
    __builtin_memcpy(&b, &d, sizeof b);
    return b;
}

static void narrow(unsigned long long hi, unsigned long long lo, unsigned long long want,
                   const char *what) {
    unsigned long long in[2] = {lo, hi};
    unsigned long long got = to_bits(__binary128_to_double(in));
    CHECK(got == want, "%s: got %016llx, want %016llx", what, got, want);
}

static void test_narrowing_rounds_to_nearest_even(void) {
    narrow(0x3fff000000000000ull, 0, 0x3ff0000000000000ull, "1.0");
    narrow(0xc000000000000000ull, 0, 0xc000000000000000ull, "-2.0");
    // 1 + 2^-53, halfway between 1 and the next double: ties to even (down).
    narrow(0x3fff000000000000ull, 1ull << 59, 0x3ff0000000000000ull, "1 + 2^-53 ties to even");
    narrow(0x3fff000000000000ull, (1ull << 59) | 1, 0x3ff0000000000001ull,
           "just above the half rounds up");
    narrow(0x3fff000000000000ull, (1ull << 59) - 1, 0x3ff0000000000000ull,
           "just below the half rounds down");
    // 1 + 2^-52 + 2^-53: halfway again, but from an odd mantissa: up.
    narrow(0x3fff000000000000ull, (1ull << 60) | (1ull << 59), 0x3ff0000000000002ull,
           "ties to even from odd");
    // All fraction bits set below 2: rounds up to 2.0 (carry into the exponent).
    narrow(0x3fffffffffffffffull, ~0ull, 0x4000000000000000ull, "carry into the exponent");
}

static void test_narrowing_range(void) {
    narrow(0x43feffffffffffffull, 0xf000000000000000ull, 0x7fefffffffffffffull, "DBL_MAX");
    narrow(0x43feffffffffffffull, 0xf800000000000000ull, 0x7ff0000000000000ull,
           "halfway past DBL_MAX overflows");
    narrow(0x43ff000000000000ull, 0, 0x7ff0000000000000ull, "2^1024 overflows");
    narrow(0x7ffe000000000000ull, 0, 0x7ff0000000000000ull, "largest binary128 overflows");
    narrow(0x3bcd000000000000ull, 0, 1, "2^-1074, the smallest subnormal");
    narrow(0x3bcc000000000000ull, 0, 0, "2^-1075 ties to even (zero)");
    narrow(0x3bcc800000000000ull, 0, 1, "1.5 * 2^-1075 rounds up");
    narrow(0x3c00ffffffffffffull, 0xe000000000000000ull, 0x000fffffffffffffull,
           "the largest subnormal");
    narrow(0x3c00ffffffffffffull, 0xf000000000000000ull, 0x0010000000000000ull,
           "rounds up into DBL_MIN");
    narrow(0x0001000000000000ull, 0, 0, "tiny binary128 normal: zero");
    narrow(0x8000000000000001ull, 0, 0x8000000000000000ull, "negative binary128 subnormal: -0");
    narrow(0x7fff000000000000ull, 0, 0x7ff0000000000000ull, "infinity");
    narrow(0xffff000000000000ull, 0, 0xfff0000000000000ull, "-infinity");
    unsigned long long in[2] = {1, 0x7fff000000000000ull};
    double nan = __binary128_to_double(in);
    CHECK(nan != nan, "a NaN with only a low payload bit stays a NaN");
}

static void test_round_trip(void) {
    unsigned long long x = 0x123456789abcdef1ull;
    int ok = 1;
    for (int i = 0; i < 100000; i++) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        double d;
        __builtin_memcpy(&d, &x, sizeof d);
        unsigned long long wide[2];
        __double_to_binary128(d, wide);
        double back = __binary128_to_double(wide);
        ok &= d != d ? back != back : to_bits(back) == x;
    }
    CHECK(ok, "double -> binary128 -> double is the identity");
}

TEST_MAIN(test_normal_values, test_zeros_and_subnormals, test_infinities_and_nans,
          test_narrowing_rounds_to_nearest_even, test_narrowing_range, test_round_trip)
