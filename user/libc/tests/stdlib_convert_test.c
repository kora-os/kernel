// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the number conversions in src/stdlib/convert.c: atoi, atol,
// atoll, atof and the strto* family. Covers what the implementation promises:
// leading white space, an optional sign, digits in the requested base, the
// 0/0x base prefixes, and the end pointer left after the last character used.
// Overflow, errno and exponent/inf/nan forms are not implemented yet and are
// deliberately not tested here.

#include "test.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

static int near_enough(long double got, long double want) {
    long double diff = got > want ? got - want : want - got;
    return diff < 1e-9L;
}

static void test_atoi_family(void) {
    CHECK(atoi("0") == 0, "atoi(0)");
    CHECK(atoi("42") == 42, "atoi(42)");
    CHECK(atoi("-42") == -42, "atoi(-42)");
    CHECK(atoi("+7") == 7, "atoi(+7)");
    CHECK(atoi("  \t\n\v\f\r123") == 123, "atoi skips all six white-space characters");
    CHECK(atoi("12abc") == 12, "atoi stops at the first non-digit");
    CHECK(atoi("abc") == 0, "atoi(abc)");
    CHECK(atoi("") == 0, "atoi(empty)");
    CHECK(atoi("- 5") == 0, "atoi does not allow space after the sign");
    CHECK(atoi("--5") == 0, "atoi(--5)");
    CHECK(atoi("2147483647") == 2147483647, "atoi(INT_MAX)");
    CHECK(atoi("-2147483647") == -2147483647, "atoi(-INT_MAX)");

    CHECK(atol("2147483648") == 2147483648L, "atol beyond 32 bits");
    CHECK(atol("-1234567890123") == -1234567890123L, "atol negative");
    CHECK(atol("  9 ") == 9, "atol trailing space");

    CHECK(atoll("1234567890123456789") == 1234567890123456789LL, "atoll large");
    CHECK(atoll("-9223372036854775807") == -9223372036854775807LL, "atoll -LLONG_MAX");
    CHECK(atoll("x") == 0, "atoll(x)");
}

static void test_atof(void) {
    CHECK(near_enough(atof("0"), 0.0), "atof(0)");
    CHECK(near_enough(atof("3.14159"), 3.14159), "atof(3.14159)");
    CHECK(near_enough(atof("-2.5"), -2.5), "atof(-2.5)");
    CHECK(near_enough(atof("+0.125"), 0.125), "atof(+0.125)");
    CHECK(near_enough(atof(".5"), 0.5), "atof(.5)");
    CHECK(near_enough(atof("5."), 5.0), "atof(5.)");
    CHECK(near_enough(atof("  \t42.75abc"), 42.75), "atof skips white space, stops at junk");
    CHECK(near_enough(atof("1234567.891"), 1234567.891), "atof larger value");
    CHECK(near_enough(atof("none"), 0.0), "atof(none)");
}

static void test_strtol(void) {
    char *end;
    const char *s = "  -123xyz";
    CHECK(strtol(s, &end, 10) == -123, "strtol(-123)");
    CHECK(end == s + 6, "strtol end pointer");

    CHECK(strtol("ff", NULL, 16) == 255, "strtol ff base 16");
    CHECK(strtol("FF", NULL, 16) == 255, "strtol FF base 16");
    CHECK(strtol("1011", NULL, 2) == 11, "strtol base 2");
    CHECK(strtol("zz", NULL, 36) == 1295, "strtol zz base 36");
    CHECK(strtol("Zz", NULL, 36) == 1295, "strtol Zz base 36");
    CHECK(strtol("777", NULL, 8) == 511, "strtol base 8");

    CHECK(strtol("0x1f", NULL, 0) == 31, "strtol base 0 hex");
    CHECK(strtol("0X1F", NULL, 0) == 31, "strtol base 0 HEX");
    CHECK(strtol("017", NULL, 0) == 15, "strtol base 0 octal");
    CHECK(strtol("42", NULL, 0) == 42, "strtol base 0 decimal");
    CHECK(strtol("0", NULL, 0) == 0, "strtol base 0 zero");
    CHECK(strtol("  -0x10", NULL, 0) == -16, "strtol base 0 signed hex");

    s = "129";
    CHECK(strtol(s, &end, 8) == 10, "strtol stops at a digit outside the base");
    CHECK(end == s + 2, "strtol end pointer at the out-of-base digit");

    s = "99 bottles";
    CHECK(strtol(s, &end, 10) == 99, "strtol(99 bottles)");
    CHECK(end == s + 2, "strtol end pointer at the space");

    s = "12";
    strtol(s, &end, 10);
    CHECK(*end == '\0', "strtol end pointer at the terminator");

    CHECK(strtol("5", NULL, 10) == 5, "strtol accepts a NULL end pointer");
}

static void test_strtoll(void) {
    char *end;
    const char *s = "-9223372036854775807!";
    CHECK(strtoll(s, &end, 10) == -9223372036854775807LL, "strtoll -LLONG_MAX");
    CHECK(end == s + 20, "strtoll end pointer");
    CHECK(strtoll("7fffffffffffffff", NULL, 16) == 9223372036854775807LL,
          "strtoll LLONG_MAX in hex");
    CHECK(strtoll("0x7f", NULL, 0) == 127, "strtoll base 0 hex");
}

static void test_strtoul(void) {
    char *end;
    const char *s = "4294967295 ";
    CHECK(strtoul(s, &end, 10) == 4294967295UL, "strtoul 2^32-1");
    CHECK(end == s + 10, "strtoul end pointer");
    CHECK(strtoul("-1", NULL, 10) == (unsigned long)-1, "strtoul(-1) negates modulo 2^N");
    CHECK(strtoul("0xdeadbeef", NULL, 0) == 0xdeadbeefUL, "strtoul base 0 hex");
    CHECK(strtoul("755", NULL, 8) == 0755, "strtoul base 8");
}

static void test_strtoull(void) {
    char *end;
    const char *s = "18446744073709551615.";
    CHECK(strtoull(s, &end, 10) == 18446744073709551615ULL, "strtoull ULLONG_MAX");
    CHECK(end == s + 20, "strtoull end pointer");
    CHECK(strtoull("ffffffffffffffff", NULL, 16) == 18446744073709551615ULL,
          "strtoull ULLONG_MAX in hex");
    CHECK(strtoull("010", NULL, 0) == 8, "strtoull base 0 octal");
}

static void test_integer_range_errors(void) {
    char *end;
    const char *s;

    errno = 0;
    CHECK(strtol("9223372036854775807", NULL, 10) == LONG_MAX && errno == 0,
          "strtol LONG_MAX is in range");
    CHECK(strtol("-9223372036854775808", NULL, 10) == LONG_MIN && errno == 0,
          "strtol LONG_MIN is in range");
    s = "9223372036854775808 tail";
    CHECK(strtol(s, &end, 10) == LONG_MAX && errno == ERANGE, "strtol LONG_MAX + 1 clamps");
    CHECK(end == s + 19, "strtol consumes every digit of an overflowing number");
    errno = 0;
    CHECK(strtol("-9223372036854775809", NULL, 10) == LONG_MIN && errno == ERANGE,
          "strtol LONG_MIN - 1 clamps");
    errno = 0;
    s = "99999999999999999999999999999999999999x";
    CHECK(strtol(s, &end, 10) == LONG_MAX && errno == ERANGE && *end == 'x',
          "strtol far past the range");
    errno = 0;
    CHECK(strtol("-0x8000000000000000", NULL, 16) == LONG_MIN && errno == 0,
          "strtol LONG_MIN in hex");
    CHECK(strtol("0x8000000000000000", NULL, 16) == LONG_MAX && errno == ERANGE,
          "strtol 2^63 in hex clamps");

    errno = 0;
    CHECK(strtoll("-9223372036854775808", NULL, 10) == LLONG_MIN && errno == 0, "strtoll LLONG_MIN");
    CHECK(strtoll("9223372036854775808", NULL, 10) == LLONG_MAX && errno == ERANGE,
          "strtoll overflow");

    errno = 0;
    CHECK(strtoul("18446744073709551615", NULL, 10) == ULONG_MAX && errno == 0,
          "strtoul ULONG_MAX is in range");
    CHECK(strtoul("18446744073709551616", NULL, 10) == ULONG_MAX && errno == ERANGE,
          "strtoul ULONG_MAX + 1 clamps");
    errno = 0;
    CHECK(strtoul("-18446744073709551615", NULL, 10) == 1 && errno == 0,
          "strtoul negates a full-width magnitude");
    CHECK(strtoul("-18446744073709551616", NULL, 10) == ULONG_MAX && errno == ERANGE,
          "strtoul negative overflow clamps to ULONG_MAX");
    errno = 0;
    CHECK(strtoull("1" "0000000000000000000000", NULL, 10) == ULLONG_MAX && errno == ERANGE,
          "strtoull overflow");

    // atoi and friends clamp like strtol before narrowing.
    errno = 0;
    CHECK(atol("-9223372036854775809") == LONG_MIN && errno == ERANGE, "atol clamps");
    errno = 0;
    CHECK(atoll("9223372036854775808") == LLONG_MAX && errno == ERANGE, "atoll clamps");
}

static void test_integer_errno_untouched(void) {
    errno = 12345;
    strtol("42", NULL, 10);
    strtoul("-1", NULL, 10);
    strtoll("", NULL, 10);
    strtoull("junk", NULL, 0);
    CHECK(errno == 12345, "successful and empty conversions leave errno alone");
    errno = 0;
}

static void test_integer_edges(void) {
    char *end;
    const char *s;

    s = "   ";
    CHECK(strtol(s, &end, 10) == 0 && end == s, "only white space: no digits, end at the start");
    s = "  -x";
    CHECK(strtol(s, &end, 10) == 0 && end == s, "sign without digits: end at the start");
    s = "+";
    CHECK(strtoul(s, &end, 10) == 0 && end == s, "a lone sign");
    s = "0x";
    CHECK(strtol(s, &end, 0) == 0 && end == s + 1, "0x with no hex digit is 0 followed by x");
    s = "0xg";
    CHECK(strtol(s, &end, 16) == 0 && end == s + 1, "0xg in base 16 is 0 followed by xg");
    s = "0x1A";
    CHECK(strtol(s, &end, 16) == 26 && end == s + 4, "base 16 accepts the 0x prefix");
    s = "0x1A";
    CHECK(strtol(s, &end, 10) == 0 && end == s + 1, "base 10 stops at x");
    s = "08";
    CHECK(strtol(s, &end, 0) == 0 && end == s + 1, "base 0 octal stops at 8");
    CHECK(strtol("-0", NULL, 10) == 0, "-0");
    CHECK(strtol("+0x7fffffffffffffff", NULL, 0) == LONG_MAX, "+ with a hex prefix");
    CHECK(strtol("1", NULL, 36) == 1 && strtol("Z", NULL, 36) == 35, "base 36 digits");
    s = "12";
    CHECK(strtol(s, &end, 2) == 1 && end == s + 1, "base 2 stops at 2");

    errno = 0;
    s = "123";
    CHECK(strtol(s, &end, 1) == 0 && errno == EINVAL && end == s, "base 1 is invalid");
    errno = 0;
    CHECK(strtol(s, &end, 37) == 0 && errno == EINVAL && end == s, "base 37 is invalid");
    errno = 0;
    CHECK(strtoul(s, &end, -1) == 0 && errno == EINVAL && end == s, "negative base is invalid");
    errno = 0;
}

static void test_strtod(void) {
    char *end;
    const char *s = "  -2.5xyz";
    CHECK(near_enough(strtod(s, &end), -2.5), "strtod(-2.5)");
    CHECK(end == s + 6, "strtod end pointer");

    s = "3.14159";
    CHECK(near_enough(strtod(s, &end), 3.14159), "strtod(3.14159)");
    CHECK(*end == '\0', "strtod end pointer at the terminator");

    s = ".5.5";
    CHECK(near_enough(strtod(s, &end), 0.5), "strtod(.5.5)");
    CHECK(end == s + 2, "strtod stops at the second point");

    CHECK(near_enough(strtod("42", NULL), 42.0), "strtod accepts a NULL end pointer");
}

static void test_strtof(void) {
    char *end;
    const char *s = "1.5 rest";
    float f = strtof(s, &end);
    CHECK(f > 1.499999f && f < 1.500001f, "strtof(1.5)");
    CHECK(end == s + 3, "strtof end pointer");
    f = strtof("-0.25", NULL);
    CHECK(f > -0.250001f && f < -0.249999f, "strtof(-0.25)");
}

static void test_strtold(void) {
    char *end;
    const char *s = "-123.456;";
    CHECK(near_enough(strtold(s, &end), -123.456L), "strtold(-123.456)");
    CHECK(end == s + 8, "strtold end pointer");
}

TEST_MAIN(test_atoi_family, test_atof, test_strtol, test_strtoll, test_strtoul,
          test_strtoull, test_integer_range_errors, test_integer_errno_untouched,
          test_integer_edges, test_strtod, test_strtof, test_strtold)
