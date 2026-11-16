// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the scanf family (src/stdio/scan.c, sscanf.c, scanf.c).
//
// 1. By hand: every conversion, length modifier, width, suppression, %n, the
//    scan sets, white space and literals in the format, the return value
//    (count, or EOF when the input ends first), and the standard's rule for
//    items that begin like a number but are not one ("0x", "1e+", "infin"),
//    on which the host libraries do not agree with each other.
// 2. Differential: well-formed input, random numbers and strings in many
//    formats, against the host's sscanf: same return value, same stored
//    values, same count of characters read.
// 3. Floats against the libc's own strtod: the engine hands strtod a text of
//    bounded length in place of the item, which must convert to the same
//    bits, for thousands of digits too.
// 4. Streams: fscanf on files of the fake kernel across buffer boundaries
//    and short reads, what is left for the next read, and scanf on the
//    console's raw, byte-at-a-time stdin.

#include "test.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_syscall.h"

// The host's sscanf, bound by symbol name. glibc's plain sscanf symbol is
// the old GNU one, where %a allocates; the ISO C99 one has its own name.
#ifdef __linux__
#define HOST_SSCANF "__isoc99_sscanf"
#else
#define HOST_SSCANF "sscanf"
#endif
int host_sscanf(const char *s, const char *format, ...)
    __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) HOST_SSCANF);

static uint64_t rng = 0x853c49e6748fea9bull;

static uint64_t next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static uint64_t dbits(double d) {
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}

static int same_double(double a, double b) {
    return dbits(a) == dbits(b) || (a != a && b != b);
}

static void test_integers(void) {
    int i = 0;
    unsigned u = 0;
    CHECK(sscanf("42", "%d", &i) == 1 && i == 42, "%%d");
    CHECK(sscanf("  -17", "%d", &i) == 1 && i == -17, "%%d skips white space, takes a sign");
    CHECK(sscanf("+8", "%d", &i) == 1 && i == 8, "%%d with a plus sign");
    CHECK(sscanf("0x1f", "%d", &i) == 1 && i == 0, "%%d stops at the x of 0x");
    CHECK(sscanf("017", "%d", &i) == 1 && i == 17, "%%d reads a leading zero as decimal");
    CHECK(sscanf("0x1F", "%i", &i) == 1 && i == 31, "%%i hexadecimal");
    CHECK(sscanf("-0x10", "%i", &i) == 1 && i == -16, "%%i negative hexadecimal");
    CHECK(sscanf("017", "%i", &i) == 1 && i == 15, "%%i octal");
    CHECK(sscanf("19", "%i", &i) == 1 && i == 19, "%%i decimal");
    CHECK(sscanf("08", "%i", &i) == 1 && i == 0, "%%i: 8 is no octal digit");
    CHECK(sscanf("777", "%o", &u) == 1 && u == 0777, "%%o");
    CHECK(sscanf("4294967295", "%u", &u) == 1 && u == 4294967295u, "%%u");
    CHECK(sscanf("-1", "%u", &u) == 1 && u == UINT_MAX, "%%u of -1 wraps, as strtoul does");
    CHECK(sscanf("ff", "%x", &u) == 1 && u == 255, "%%x");
    CHECK(sscanf("0XfF", "%X", &u) == 1 && u == 255, "%%X with a prefix");
    CHECK(sscanf("12ab", "%d", &i) == 1 && i == 12, "a number ends at the first other character");

    // Every length modifier stores a value of its own size and nothing more.
#define SIZE_CHECK(format, type, text, want)                                      \
    do {                                                                          \
        union {                                                                   \
            type value;                                                           \
            unsigned char bytes[16];                                              \
        } slot;                                                                   \
        memset(&slot, 0xaa, sizeof slot);                                         \
        int r = sscanf(text, format, &slot.value);                                \
        int tail = 1;                                                             \
        for (size_t k = sizeof(type); k < sizeof slot; k++) {                     \
            tail &= slot.bytes[k] == 0xaa;                                        \
        }                                                                         \
        CHECK(r == 1 && slot.value == (type)(want) && tail, "%s stores a %s", format, #type); \
    } while (0)
    SIZE_CHECK("%hhd", signed char, "-3", -3);
    SIZE_CHECK("%hhu", unsigned char, "258", 2);
    SIZE_CHECK("%hd", short, "-30000", -30000);
    SIZE_CHECK("%hx", unsigned short, "1ffff", 0xffff);
    SIZE_CHECK("%d", int, "-2000000000", -2000000000);
    SIZE_CHECK("%ld", long, "-9000000000", -9000000000l);
    SIZE_CHECK("%lu", unsigned long, "18446744073709551615", ULONG_MAX);
    SIZE_CHECK("%lld", long long, "9223372036854775807", LLONG_MAX);
    SIZE_CHECK("%llx", unsigned long long, "123456789abcdef0", 0x123456789abcdef0ull);
    SIZE_CHECK("%jd", intmax_t, "-9223372036854775808", INTMAX_MIN);
    SIZE_CHECK("%zu", size_t, "1234567890123", (size_t)1234567890123ull);
    SIZE_CHECK("%td", ptrdiff_t, "-5", -5);
    SIZE_CHECK("%hhi", signed char, "0x7f", 127);
    SIZE_CHECK("%lo", unsigned long, "17", 15);

    // Out of range: clamped as strtoll and strtoull clamp.
    long long ll = 0;
    unsigned long long ull = 0;
    CHECK(sscanf("99999999999999999999999", "%lld", &ll) == 1 && ll == LLONG_MAX, "%%lld clamps");
    CHECK(sscanf("-99999999999999999999999", "%lld", &ll) == 1 && ll == LLONG_MIN,
          "%%lld clamps downwards");
    CHECK(sscanf("99999999999999999999999", "%llu", &ull) == 1 && ull == ULLONG_MAX,
          "%%llu clamps");
    CHECK(sscanf("0000000000000000000000000000000000000000012", "%lld", &ll) == 1 && ll == 12,
          "leading zeros are no overflow");

    void *p = NULL;
    char text[32];
    snprintf(text, sizeof text, "%p", (void *)&i);
    CHECK(sscanf(text, "%p", &p) == 1 && p == (void *)&i, "%%p reads what printf's %%p writes");
    CHECK(sscanf("0x0", "%p", &p) == 1 && p == NULL, "%%p of a null pointer");
}

static void test_widths(void) {
    int a = 0, b = 0, c = 0;
    CHECK(sscanf("123456", "%2d%3d%d", &a, &b, &c) == 3 && a == 12 && b == 345 && c == 6,
          "widths cut a run of digits");
    CHECK(sscanf("-123", "%3d%d", &a, &b) == 2 && a == -12 && b == 3, "the sign counts");
    CHECK(sscanf("0x1f", "%3i%x", &a, &b) == 2 && a == 1 && b == 0xf, "and so does the prefix");
    CHECK(sscanf("  42", "%2d", &a) == 1 && a == 42, "leading white space does not");
    double d = 0;
    CHECK(sscanf("3.14159", "%4lf%d", &d, &a) == 2 && d == 3.14 && a == 159, "a float's width");
    CHECK(sscanf("1e5", "%3lf", &d) == 1 && d == 1e5, "a width that fits the exponent");
    char s[8];
    CHECK(sscanf("abcdefgh", "%3s%2s", s, s + 4) == 2 && strcmp(s, "abc") == 0 &&
              strcmp(s + 4, "de") == 0,
          "%%3s stores 3 characters and a NUL");
    CHECK(sscanf("12", "%0d", &a) == 1 && a == 12, "a width of 0 is no width");
}

// Items that start like a number but are not one fail, and what was read
// stays read. Whether the failure is "matching" (0) or "input" (EOF) depends
// on what stopped the item: a character that does not fit, or the end.
static void test_incomplete_numbers(void) {
    int i = 5, n = -1;
    unsigned u = 5;
    double d = 5;
    char rest[16] = "";
    CHECK(sscanf("-", "%d", &i) == EOF && i == 5, "a sign and the end: EOF");
    CHECK(sscanf("-x", "%d", &i) == 0 && i == 5, "a sign and something else: 0");
    CHECK(sscanf("x", "%d", &i) == 0, "no digits: 0");
    CHECK(sscanf("", "%d", &i) == EOF, "empty input: EOF");
    CHECK(sscanf("   ", "%d", &i) == EOF, "only white space: EOF");
    CHECK(sscanf("0x", "%x", &u) == EOF && u == 5, "0x and the end: EOF");
    CHECK(sscanf("0xz", "%x", &u) == 0 && u == 5, "0x and no digit: fails, unlike strtoul");
    CHECK(sscanf("0xz", "%i%n", &i, &n) == 0 && n == -1, "the same for %%i");
    CHECK(sscanf("0xz", "%d%s", &i, rest) == 2 && i == 0 && strcmp(rest, "xz") == 0,
          "but %%d never looks at the x");
    CHECK(sscanf("-", "%1d", &i) == 0, "a width that ends on the sign: 0");

    CHECK(sscanf("1e", "%lf", &d) == EOF && d == 5, "1e and the end: EOF");
    CHECK(sscanf("1e+", "%lf", &d) == EOF, "1e+ and the end: EOF");
    CHECK(sscanf("1ex", "%lf", &d) == 0 && d == 5, "1e and no digit: fails, unlike strtod");
    CHECK(sscanf("1e+x", "%lf", &d) == 0, "1e+ and no digit");
    CHECK(sscanf(".", "%lf", &d) == EOF && sscanf(".x", "%lf", &d) == 0, "a lone point");
    CHECK(sscanf("+.e1", "%lf", &d) == 0, "a point with no digits");
    CHECK(sscanf("0x", "%lf", &d) == EOF && sscanf("0xp1", "%lf", &d) == 0, "0x with no digits");
    CHECK(sscanf("0x1p", "%lf", &d) == EOF && sscanf("0x1pz", "%lf", &d) == 0,
          "a p with no exponent");
    CHECK(sscanf("in", "%lf", &d) == EOF && sscanf("inx", "%lf", &d) == 0, "part of inf");
    CHECK(sscanf("infinit", "%lf", &d) == EOF && sscanf("infinite", "%lf", &d) == 0,
          "inf must go on to infinity or stop");
    CHECK(sscanf("na", "%lf", &d) == EOF && sscanf("nab", "%lf", &d) == 0, "part of nan");
    CHECK(sscanf("nan(abc", "%lf", &d) == EOF && sscanf("nan(a b)", "%lf", &d) == 0,
          "nan( must be closed");
    CHECK(sscanf("1e5", "%2lf", &d) == 0, "a width that ends on the e");
    CHECK(d == 5, "none of these stored anything");

    // After a failure the item's characters are gone, but for the last one.
    CHECK(sscanf("1e+x", "%*lf%n", &n) == 0 && n == -1, "nothing after a failure runs");
    CHECK(sscanf("12 0xz 7", "%d %x %d", &i, &u, &n) == 1 && i == 12, "the count so far");
}

static void test_floats(void) {
    float f = 0;
    double d = 0;
    long double ld = 0;
    CHECK(sscanf("3.5", "%f", &f) == 1 && f == 3.5f, "%%f stores a float");
    CHECK(sscanf("3.5", "%lf", &d) == 1 && d == 3.5, "%%lf stores a double");
    CHECK(sscanf("3.5", "%Lf", &ld) == 1 && (double)ld == 3.5, "%%Lf stores a long double");
    CHECK(sscanf("-2.5e3", "%le", &d) == 1 && d == -2500.0, "%%le");
    CHECK(sscanf("1E-2", "%lg", &d) == 1 && d == 0.01, "%%lg with an upper case E");
    CHECK(sscanf("0x1.8p1", "%la", &d) == 1 && d == 3.0, "%%la hexadecimal");
    CHECK(sscanf("0X.8P+2", "%lf", &d) == 1 && d == 2.0, "hexadecimal with any conversion");
    CHECK(sscanf(".5", "%lf", &d) == 1 && d == 0.5, "no digit before the point");
    CHECK(sscanf("5.", "%lf", &d) == 1 && d == 5.0, "no digit after it");
    CHECK(sscanf("5.e1", "%lf", &d) == 1 && d == 50.0, "nor before the exponent");
    CHECK(sscanf("-0", "%lf", &d) == 1 && dbits(d) == 1ull << 63, "minus zero keeps its sign");
    CHECK(sscanf("-0.000e99", "%lf", &d) == 1 && dbits(d) == 1ull << 63, "with an exponent too");
    CHECK(sscanf("inf", "%lf", &d) == 1 && d > DBL_MAX, "inf");
    CHECK(sscanf("-INFINITY", "%lf", &d) == 1 && d < -DBL_MAX, "-INFINITY");
    CHECK(sscanf("nan", "%lf", &d) == 1 && d != d, "nan");
    CHECK(sscanf("NaN(x_9)", "%lf", &d) == 1 && d != d, "nan(...)");
    int n = 0;
    CHECK(sscanf("infx", "%lf%n", &d, &n) == 1 && d > DBL_MAX && n == 3, "inf, then an x");
    CHECK(sscanf("nan()x", "%lf%n", &d, &n) == 1 && n == 5, "nan() with nothing inside");
    CHECK(sscanf("1.5.2", "%lf%n", &d, &n) == 1 && d == 1.5 && n == 3, "a second point ends it");
    CHECK(sscanf("0x1.8", "%lf%n", &d, &n) == 1 && d == 1.5 && n == 5, "hexadecimal without p");
    CHECK(sscanf("1e400", "%lf", &d) == 1 && d > DBL_MAX, "overflow gives infinity");
    CHECK(sscanf("1e-400", "%lf", &d) == 1 && d == 0.0, "underflow gives zero");
    CHECK(sscanf("3.4028235e38", "%f", &f) == 1 && f == FLT_MAX, "FLT_MAX, rounded once");
    CHECK(sscanf("1e39", "%f", &f) == 1 && f > FLT_MAX, "a float overflows on its own range");
    CHECK(sscanf("0.1", "%Lf", &ld) == 1 && (double)ld == 0.1, "%%Lf has double precision");
}

static char big[6000];

// The engine does not hand the item itself to strtod, but a text of bounded
// length that must convert to the same bits.
static void compare_with_strtod(const char *text) {
    double want = strtod(text, NULL), got = -1;
    float fwant = strtof(text, NULL), fgot = -1;
    int n = -1;
    int r = sscanf(text, "%lf%n", &got, &n);
    int rf = sscanf(text, "%f", &fgot);
    CHECK(r == 1 && rf == 1 && same_double(got, want) && same_double(fgot, fwant) &&
              n == (int)strlen(text),
          "\"%.60s%s\": sscanf %a (%d read), strtod %a", text, strlen(text) > 60 ? "..." : "", got,
          n, want);
}

static void test_floats_like_strtod(void) {
    static const char *const cases[] = {
        "0", "0.0", "000.000", "1", "0.1", "9007199254740993", "9007199254740993.000000000001",
        "2.2250738585072011e-308", "4.9406564584124654e-324", "2.4703282292062328e-324",
        "1.7976931348623159e308", "123456789012345678901234567890", "1e23", "8.5e-320",
        "0.000000000000000000000000000001", "1.00000000000000011102230246251565404236316680908203125",
        "1.00000000000000011102230246251565404236316680908203126", "0x1p-1074", "0x1p-1075",
        "0x1.0000000000001p-1075", "0x1.fffffffffffff8p1023", "0x123456789abcdef0123p0",
        "0x.0000000000000000000001p0", "0x1.00000000000008p0", "0x1.000000000000080000001p0",
        "0x0.0p99", "0e99999", "1e99999999999", "1e-99999999999", "0x1p99999999", "-.5e-3",
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        compare_with_strtod(cases[i]);
    }
    // Random decimal strings over the whole range.
    char s[80];
    for (int i = 0; i < 20000; i++) {
        int digits = 1 + (int)(next_random() % 25);
        int point = (int)(next_random() % (unsigned)(digits + 1));
        size_t n = 0;
        if (next_random() & 1) {
            s[n++] = '-';
        }
        for (int d = 0; d < digits; d++) {
            if (d == point) {
                s[n++] = '.';
            }
            s[n++] = (char)('0' + next_random() % 10);
        }
        snprintf(s + n, sizeof s - n, "e%d", (int)(next_random() % 680) - 350);
        compare_with_strtod(s);
    }
    // Round trips of random doubles, decimal and hexadecimal.
    for (int i = 0; i < 20000; i++) {
        double d;
        uint64_t b = next_random();
        memcpy(&d, &b, sizeof d);
        if (d != d) {
            continue;
        }
        static const char *const formats[] = {"%.17g", "%.15e", "%a", "%.3a", "%f"};
        snprintf(big, sizeof big, formats[i % 5], d);
        compare_with_strtod(big);
    }
    // More digits than strtod keeps (800): the rest only counts as "more".
    memset(big, '0', 2000);
    big[0] = '1', big[1] = '.', big[2000] = '\0';
    compare_with_strtod(big);  // exactly 1
    big[1999] = '1';
    compare_with_strtod(big);  // a hair above 1
    memset(big, '9', 2000);
    big[1] = '.';
    compare_with_strtod(big);  // a hair below 10
    memset(big, '0', 5000);
    big[0] = '.', big[4999] = '7', big[5000] = '\0';
    compare_with_strtod(big);  // 7e-4999: zero
    memset(big, '0', 3000);
    big[0] = '1', big[3000] = '\0';
    compare_with_strtod(big);  // 1e2999: infinity
    memcpy(big + 3000, "e-2990", 7);
    compare_with_strtod(big);  // 1e2999 * 1e-2990 = 1e9
    memset(big, '0', 3000);
    memcpy(big, "0.", 2);
    memcpy(big + 2990, "125e2990", 9);
    compare_with_strtod(big);  // 2988 zeros after the point, brought back by the exponent
    // The exact midpoint between 1 and the next double, with a 1 far below it.
    snprintf(big, sizeof big, "1.00000000000000011102230246251565404236316680908203125");
    size_t len = strlen(big);
    memset(big + len, '0', 1500);
    big[len + 1500] = '\0';
    compare_with_strtod(big);  // still the midpoint: rounds to even
    big[len + 1499] = '1';
    compare_with_strtod(big);  // just above it: rounds up
    // Hexadecimal: 16 digits kept, the rest sticky.
    snprintf(big, sizeof big, "0x1.00000000000008");
    len = strlen(big);
    memset(big + len, '0', 500);
    memcpy(big + len + 500, "p0", 3);
    compare_with_strtod(big);
    big[len + 499] = '1';
    compare_with_strtod(big);
}

static void test_characters_and_strings(void) {
    char c = 0, buf[32];
    int n = 0;
    CHECK(sscanf(" x", "%c", &c) == 1 && c == ' ', "%%c does not skip white space");
    CHECK(sscanf(" x", " %c", &c) == 1 && c == 'x', "a space in the format does");
    memset(buf, '#', sizeof buf);
    CHECK(sscanf("a b c", "%5c", buf) == 1 && memcmp(buf, "a b c#", 6) == 0,
          "%%5c stores 5 characters and no NUL");
    memset(buf, '#', sizeof buf);
    CHECK(sscanf("ab", "%3c", buf) == EOF, "%%3c with only 2 characters left: EOF");
    CHECK(sscanf("", "%c", &c) == EOF, "%%c at the end: EOF");
    CHECK(sscanf("  hello world", "%s%n", buf, &n) == 1 && strcmp(buf, "hello") == 0 && n == 7,
          "%%s reads a word");
    CHECK(sscanf("a\tb\nc", "%s %s %s", buf, buf + 4, buf + 8) == 3 && buf[4] == 'b' &&
              buf[8] == 'c',
          "any white space separates");
    CHECK(sscanf("   ", "%s", buf) == EOF, "%%s with only white space: EOF");
    CHECK(sscanf("caf\xc3\xa9 x", "%s", buf) == 1 && strcmp(buf, "caf\xc3\xa9") == 0,
          "bytes above 127 are characters like any other");

    CHECK(sscanf("abc123", "%[a-z]%n", buf, &n) == 1 && strcmp(buf, "abc") == 0 && n == 3,
          "a scan set with a range");
    CHECK(sscanf("abc123", "%[^0-9]%[0-9]", buf, buf + 8) == 2 && strcmp(buf + 8, "123") == 0,
          "a negated set");
    CHECK(sscanf("a]b-c", "%[]a]%n", buf, &n) == 1 && strcmp(buf, "a]") == 0 && n == 2,
          "a ] first belongs to the set");
    CHECK(sscanf("a]b", "%[^]]", buf) == 1 && strcmp(buf, "a") == 0, "also in a negated set");
    CHECK(sscanf("a-z!", "%[a-]", buf) == 1 && strcmp(buf, "a-") == 0, "a - last is itself");
    CHECK(sscanf("-ab", "%[-a]", buf) == 1 && strcmp(buf, "-a") == 0, "a - first too");
    CHECK(sscanf("  x", "%[ ]%n", buf, &n) == 1 && n == 2, "a set does not skip white space");
    CHECK(sscanf("hello, world", "%[^,], %s", buf, buf + 16) == 2 && strcmp(buf, "hello") == 0 &&
              strcmp(buf + 16, "world") == 0,
          "fields separated by a comma");
    CHECK(sscanf("123", "%[a-z]", buf) == 0, "no character of the set: 0");
    CHECK(sscanf("", "%[a-z]", buf) == EOF, "a set at the end: EOF");
    CHECK(sscanf("abcdef", "%3[a-z]%s", buf, buf + 8) == 2 && strcmp(buf, "abc") == 0 &&
              strcmp(buf + 8, "def") == 0,
          "a set with a width");
    CHECK(sscanf("x\xff\x80y", "%[^y]", buf) == 1 && strlen(buf) == 3, "a set and high bytes");
    CHECK(sscanf("abc", "%[a-z", buf) == 0, "a set with no ]: the format is wrong");
}

static void test_format(void) {
    int a = 0, b = 0, n = -1;
    char s[16];
    CHECK(sscanf("x=5,y=7", "x=%d,y=%d", &a, &b) == 2 && a == 5 && b == 7, "literal characters");
    CHECK(sscanf("x=5;y=7", "x=%d,y=%d", &a, &b) == 1, "a literal that does not match");
    CHECK(sscanf("x=", "x=%d", &a) == EOF, "the end after a literal: EOF");
    CHECK(sscanf("x", "xy%d", &a) == EOF, "the end in the literals: EOF");
    CHECK(sscanf("5 %", "%d %%%n", &a, &n) == 1 && n == 3, "%%%% matches a percent sign");
    CHECK(sscanf("5   %", "%d%%%n", &a, &n) == 1 && n == 5, "and skips white space first");
    CHECK(sscanf("5x", "%d%%", &a) == 1, "a missing percent sign");
    CHECK(sscanf("1 2", "%d%d", &a, &b) == 2, "numbers need no space in the format");
    CHECK(sscanf("1\n\t 2", "%d %d", &a, &b) == 2, "one space in the format is any amount");
    CHECK(sscanf("12", "%d %d", &a, &b) == 1, "the end after white space");
    n = -1;
    CHECK(sscanf("12  ", "%d %n", &a, &n) == 1 && n == 4, "trailing white space is consumed");
    n = -1;
    CHECK(sscanf("12", "%d %n", &a, &n) == 1 && n == 2, "and may be absent");

    CHECK(sscanf("1 2 3", "%*d %d %*d%n", &a, &n) == 1 && a == 2 && n == 5,
          "* reads without storing or counting");
    CHECK(sscanf("abc def", "%*s %s", s) == 1 && strcmp(s, "def") == 0, "%%*s");
    CHECK(sscanf("abc", "%*d") == 0 && sscanf("", "%*d") == EOF, "a suppressed item still fails");
    CHECK(sscanf("7", "%*d") == 0, "and still succeeds, with nothing assigned");

    long ln = -1;
    signed char cn = -1;
    CHECK(sscanf("abc de", "%*s%n %*s%ln", &n, &ln) == 0 && n == 3 && ln == 6,
          "%%n is not counted, and takes a length");
    CHECK(sscanf("abcdefgh", "%*4c%hhn", &cn) == 0 && cn == 4, "%%hhn");
    CHECK(sscanf("5", "%d%n", &a, &n) == 1 && n == 1, "%%n at the end of the input");

    CHECK(sscanf("5 x", "%d %q %d", &a, &b) == 1, "an unknown conversion stops the scan");
    CHECK(sscanf("5", "%d%", &a) == 1, "a format ending in %%");
    wchar_t w[4];
    CHECK(sscanf("abc", "%ls", w) == 0 && sscanf("abc", "%lc", w) == 0 &&
              sscanf("abc", "%l[a-z]", w) == 0,
          "wide-character conversions are not supported");
    CHECK(sscanf("", "") == 0 && sscanf("abc", "") == 0, "an empty format");
    CHECK(sscanf("", " ") == 0 && sscanf("", "x") == EOF, "an empty input");
}

static int via_vsscanf(const char *s, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vsscanf(s, format, ap);
    va_end(ap);
    return r;
}

static int via_vfscanf(FILE *f, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vfscanf(f, format, ap);
    va_end(ap);
    return r;
}

static int via_vscanf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vscanf(format, ap);
    va_end(ap);
    return r;
}

// Well-formed input against the host's sscanf.
static void test_against_host(void) {
    static const char *const int_formats[] = {"%d", "%i", "%u", "%x", "%o", "%ld", "%lld",
                                              "%hd", "%hhd", "%lu", "%lx", "%zu", "%jd", "%hhu",
                                              "%5d", "%12ld", "%3x", "%20llu", "%2i"};
    static const char *const number_formats[] = {"%lld", "%llx", "%llo", "%#llx", "%llu"};
    char text[96], format[24];
    unsigned mismatches = 0;
    for (int i = 0; i < 60000; i++) {
        // A number of random size, in a random notation, after random space.
        uint64_t v = next_random() >> (next_random() % 64);
        size_t n = 0;
        for (unsigned k = (unsigned)(next_random() % 3); k > 0; k--) {
            text[n++] = " \t\n"[next_random() % 3];
        }
        if (next_random() % 4 == 0) {
            text[n++] = '-';
        }
        n += (size_t)snprintf(text + n, sizeof text - n, number_formats[next_random() % 5],
                              (unsigned long long)v);
        memcpy(text + n, " tail", 6);
        snprintf(format, sizeof format, "%s%%n", int_formats[i % 19]);
        long long want = 0x5555555555555555ll, got = 0x5555555555555555ll;
        int want_n = -1, got_n = -1;
        int want_r = host_sscanf(text, format, &want, &want_n);
        int got_r = sscanf(text, format, &got, &got_n);
        test_checks++;
        // A width that cuts the item right after "0x" leaves no number. The
        // hosts differ there: Apple's gives the 0 and keeps the x for later,
        // the standard's longest-prefix rule, followed here, fails the item
        // (test_incomplete_numbers has that case).
        int width = atoi(int_formats[i % 19] + 1);
        const char *item = text + strspn(text, " \t\n");
        if (width > 0 && (item[width - 1] == 'x' || item[width - 1] == 'X') && got_r == 0) {
            continue;
        }
        if (want_r != got_r || want != got || want_n != got_n) {
            test_failures++;
            if (++mismatches <= 20) {
                test_printf("FAIL sscanf(\"%s\", \"%s\"): libc %d %llx (%d read), host %d %llx (%d)\n",
                            text, format, got_r, got, got_n, want_r, want, want_n);
            }
        }
    }

    static const char *const float_formats[] = {"%lf", "%le", "%lg", "%la", "%f", "%Lf", "%30lf"};
    static const char *const print_formats[] = {"%.17g", "%e", "%f", "%a", "%.3g", "%.10e"};
    for (int i = 0; i < 40000; i++) {
        double d;
        uint64_t b = next_random();
        memcpy(&d, &b, sizeof d);
        if (d != d || d - d != 0) {
            continue;
        }
        if (i % 3 == 0) {
            d = (double)(long)(next_random() % 2000000) / 1000 - 1000;
        }
        size_t n = (size_t)snprintf(text, sizeof text, print_formats[next_random() % 6], d);
        if (n > 70) {
            continue;  // %f of a huge number
        }
        memcpy(text + n, ";", 2);
        const char *base = float_formats[i % 7];
        snprintf(format, sizeof format, "%s%%n", base);
        int want_n = -1, got_n = -1, want_r, got_r, equal;
        if (strcmp(base, "%f") == 0) {
            float want = -1, got = -1;
            want_r = host_sscanf(text, format, &want, &want_n);
            got_r = sscanf(text, format, &got, &got_n);
            equal = same_double(want, got);
        } else if (strcmp(base, "%Lf") == 0) {
            long double want = -1, got = -1;
            want_r = host_sscanf(text, format, &want, &want_n);
            got_r = sscanf(text, format, &got, &got_n);
            // The libc's long double has double precision, correctly
            // rounded. Where the host's is wider (x87), narrowing its result
            // rounds a second time and may land on the neighbouring double.
            uint64_t a = dbits((double)want), b2 = dbits((double)got);
            equal = LDBL_MANT_DIG == DBL_MANT_DIG ? a == b2 : (a > b2 ? a - b2 : b2 - a) <= 1;
        } else {
            double want = -1, got = -1;
            want_r = host_sscanf(text, format, &want, &want_n);
            got_r = sscanf(text, format, &got, &got_n);
            equal = same_double(want, got);
        }
        test_checks++;
        if (want_r != got_r || !equal || want_n != got_n) {
            test_failures++;
            if (++mismatches <= 20) {
                test_printf("FAIL sscanf(\"%s\", \"%s\"): libc %d (%d read), host %d (%d)\n", text,
                            format, got_r, got_n, want_r, want_n);
            }
        }
    }

    static const char *const string_formats[] = {"%s", "%5s", "%[a-m]", "%[^n-z ]", "%3c",
                                                 "%c", " %c", "%[^,]", "%2[a-z ]", "%10[^\n]"};
    for (int i = 0; i < 40000; i++) {
        size_t len = next_random() % 12, n = 0;
        while (n < len) {
            text[n++] = "abcmnxyz ,\n-"[next_random() % 12];
        }
        text[n] = '\0';
        snprintf(format, sizeof format, "%s%%n", string_formats[i % 10]);
        char want[32], got[32];
        memset(want, '#', sizeof want);
        memset(got, '#', sizeof got);
        int want_n = -1, got_n = -1;
        int want_r = host_sscanf(text, format, want, &want_n);
        int got_r = sscanf(text, format, got, &got_n);
        test_checks++;
        // "%3c" with fewer than 3 characters left: glibc stores what there is
        // and reports success; the standard asks for exactly 3.
        if (i % 10 == 4 && len > 0 && len < 3) {
            continue;
        }
        if (want_r != got_r || want_n != got_n || (got_r == 1 && memcmp(want, got, sizeof got) != 0)) {
            test_failures++;
            if (++mismatches <= 20) {
                test_printf("FAIL sscanf(\"%s\", \"%s\"): libc %d (%d read), host %d (%d)\n", text,
                            format, got_r, got_n, want_r, want_n);
            }
        }
    }
}

static void test_streams(void) {
    // A table larger than the stream's buffer, read back field by field.
    static char table[40000];
    size_t len = 0;
    for (int i = 0; i < 1500; i++) {
        len += (size_t)snprintf(table + len, sizeof table - len, "%d  item%d\t%.3f\n", i * 7 - 300,
                                i, i / 8.0);
    }
    for (unsigned chunk = 0; chunk <= 7; chunk += 7) {  // whole reads, then short ones
        fake_reset_files();
        fake_put_file("table.txt", table, len);
        fake_set_file_chunks(chunk, 0);
        FILE *f = fopen("table.txt", "r");
        CHECK(f != NULL, "fopen");
        int rows = 0, good = 1;
        for (;;) {
            int number = 0;
            char name[16], want[16];
            double value = 0;
            int r = fscanf(f, "%d %15s %lf", &number, name, &value);
            if (r != 3) {
                CHECK(r == EOF && feof(f) && !ferror(f), "fscanf returns EOF at the end: %d", r);
                break;
            }
            snprintf(want, sizeof want, "item%d", rows);
            good &= number == rows * 7 - 300 && strcmp(name, want) == 0 && value == rows / 8.0;
            rows++;
        }
        CHECK(rows == 1500 && good, "1500 rows read back (read chunk %u): %d", chunk, rows);
        fclose(f);
    }
    fake_set_file_chunks(0, 0);

    // What an item leaves is what the stream delivers next.
    fake_put_file("mix.txt", "12,abc\nrest of line\n0xz", 23);
    FILE *f = fopen("mix.txt", "r");
    int n = 0;
    char buf[32];
    CHECK(fscanf(f, "%d", &n) == 1 && n == 12 && fgetc(f) == ',', "the comma after the number");
    CHECK(fscanf(f, "%[a-z]", buf) == 1 && fgetc(f) == '\n', "the newline after the set");
    CHECK(fgets(buf, sizeof buf, f) != NULL && strcmp(buf, "rest of line\n") == 0,
          "fgets carries on from there");
    unsigned u = 9;
    CHECK(fscanf(f, "%x", &u) == 0 && u == 9 && fgetc(f) == 'z',
          "a failed 0x keeps one character back, not two");
    CHECK(fscanf(f, "%d", &n) == EOF && feof(f), "then the end");
    fclose(f);

    fake_put_file("v.txt", "7 8", 3);
    f = fopen("v.txt", "r");
    int a = 0, b = 0;
    CHECK(via_vfscanf(f, "%d %d", &a, &b) == 2 && a == 7 && b == 8, "vfscanf");
    fclose(f);
    CHECK(via_vsscanf("3 4", "%d %d", &a, &b) == 2 && a == 3 && b == 4, "vsscanf");

    f = fopen("out.txt", "w");
    CHECK(fscanf(f, "%d", &n) == EOF && ferror(f), "fscanf on a stream opened for writing");
    fclose(f);

    // stdin: the console hands bytes over one at a time, Enter is '\r'.
    fake_reset_console();
    fake_set_input("42\rabc  -7\r", 11, 1);
    printf("number? ");
    CHECK(scanf("%d", &n) == 1 && n == 42, "scanf reads a number typed on the console");
    CHECK(strcmp(fake_output(1, NULL), "number? ") == 0, "after the prompt was flushed");
    CHECK(fake_input_position() == 3, "it read one byte past the number: %zu",
          fake_input_position());
    CHECK(getchar() == '\r', "which the next read still gets");
    CHECK(via_vscanf("%3s%d", buf, &n) == 2 && strcmp(buf, "abc") == 0 && n == -7, "vscanf");
    CHECK(scanf("%d", &n) == EOF, "the end of the input");
    fake_reset_console();
    fake_reset_files();
}

TEST_MAIN(test_integers, test_widths, test_incomplete_numbers, test_floats,
          test_floats_like_strtod, test_characters_and_strings, test_format, test_against_host,
          test_streams)
