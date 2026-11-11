// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for strtod and strtof (src/stdlib/strtod.c). The host's own
// strtod and strtof (glibc, Apple's libc) are correctly rounded, so they are
// the reference: every input must give the same bits and the same end
// pointer. Inputs: hand-picked hard cases, random decimal strings over the
// whole range, round trips of random doubles in several formats (%.17g,
// %.15e, %a), and the hardest cases of all, the exact decimal midpoints
// between neighbouring doubles (up to 767 significant digits), alone and
// with a trailing digit just above or below. Special forms (inf, nan, hex,
// end pointers, errno) are checked by hand too.

#include "test.h"

#include <errno.h>
#include <float.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#undef strtod
#undef strtof
#undef snprintf
double strtod(const char *s, char **end);
float strtof(const char *s, char **end);
int snprintf(char *s, size_t n, const char *fmt, ...);

static uint64_t dbits(double d) {
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}

static uint32_t fbits(float f) {
    uint32_t b;
    memcpy(&b, &f, sizeof b);
    return b;
}

static double from_dbits(uint64_t b) {
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static unsigned mismatches;

static void compare(const char *s) {
    char *want_end, *got_end;
    double want = strtod(s, &want_end);
    double got = kora_strtod(s, &got_end);
    float fwant = strtof(s, NULL);
    float fgot = kora_strtof(s, NULL);
    // NaN payloads may differ between libcs; any NaN matches any NaN.
    int same = (dbits(want) == dbits(got) || (want != want && got != got)) && want_end == got_end;
    int fsame = fbits(fwant) == fbits(fgot) || (fwant != fwant && fgot != fgot);
    test_checks += 2;
    if (!same || !fsame) {
        test_failures++;
        if (++mismatches <= 20) {
            printf("FAIL \"%.80s%s\": host %016llx (+%d) %08x, libc %016llx (+%d) %08x\n", s,
                   strlen(s) > 80 ? "..." : "", (unsigned long long)dbits(want),
                   (int)(want_end - s), fbits(fwant), (unsigned long long)dbits(got),
                   (int)(got_end - s), fbits(fgot));
        }
    }
}

static void test_hand_picked(void) {
    static const char *const cases[] = {
        "0", "-0", "+0.0", "1", "-1", "0.1", "0.5", "1.5", "2.5", "3.14159", "1e10", "1E-10",
        "123456789012345678901234567890", "0.000000000000000000000000000001",
        "9007199254740992", "9007199254740993", "9007199254740994", "9007199254740995",
        "9007199254740993.000000000000000000000000000001",
        "2.2250738585072011e-308", "2.2250738585072012e-308", "2.2250738585072014e-308",
        "4.9406564584124654e-324", "2.4703282292062327e-324", "2.4703282292062328e-324",
        "1e-324", "3e-324", "1.7976931348623157e308", "1.7976931348623158e308",
        "1.7976931348623159e308", "1e309", "1e-400", "0e1000", "0e-1000",
        "1.00000000000000011102230246251565404236316680908203125",
        "1.00000000000000011102230246251565404236316680908203124",
        "1.00000000000000011102230246251565404236316680908203126",
        "3.4028234663852886e38", "3.4028235677973366e38", "1.1754943508222875e-38",
        "1.401298464324817e-45", "7.006492321624085e-46", "7.006492321624086e-46",
        "  \t\n 42.5xyz", "1e", "1e+", "1e-", "1e+5x", ".5", "5.", ".", "-.", "+.e1", "e5",
        "0x", "0x1", "0X1P3", "0x1.8p1", "0x.8p1", "0x1p", "0x1p-1074", "0x1p-1075",
        "0x1.0000000000001p-1075", "0x1.fffffffffffff8p1023", "0x1.fffffffffffffp1023",
        "0x123456789abcdef0123p0", "0x.0000000000000000000001p0", "0xg", "0x.p1",
        "inf", "-INF", "infinity", "InFiNiTy", "infinit", "nan", "-NaN", "nan(123)",
        "nan(abc_9)", "nan(", "1e99999999999999999", "1e-99999999999999999",
        "000000000000000000000000000000000001.5", "0.0000000000000000000000000000000000000",
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        compare(cases[i]);
    }
}

static uint64_t rng = 0x243f6a8885a308d3ull;

static uint64_t next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static void test_random_decimals(void) {
    char s[64];
    for (int i = 0; i < 40000; i++) {
        int digits = 1 + (int)(next_random() % 25);
        int point = (int)(next_random() % (unsigned)(digits + 1));
        int exp = (int)(next_random() % 680) - 350;
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
        n += (size_t)snprintf(s + n, sizeof s - n, "e%d", exp);
        compare(s);
    }
}

static void test_round_trips(void) {
    char s[64];
    for (int i = 0; i < 20000; i++) {
        double d = from_dbits(next_random());
        if (d != d) {
            continue;
        }
        static const char *const formats[] = {"%.17g", "%.16g", "%.15e", "%.9g", "%a", "%.3a"};
        for (size_t f = 0; f < sizeof formats / sizeof formats[0]; f++) {
            snprintf(s, sizeof s, formats[f], d);
            compare(s);
        }
    }
}

// Decimal digits of a big integer, most significant first, in a char array.
struct digits {
    char d[1200];
    int n;
};

static void digits_set(struct digits *x, uint64_t v) {
    char rev[24];
    int n = 0;
    do {
        rev[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    for (x->n = 0; n > 0;) {
        x->d[x->n++] = rev[--n];
    }
}

static void digits_mul(struct digits *x, unsigned f) {
    unsigned carry = 0;
    for (int i = x->n - 1; i >= 0; i--) {
        unsigned v = (unsigned)(x->d[i] - '0') * f + carry;
        x->d[i] = (char)('0' + v % 10);
        carry = v / 10;
    }
    while (carry != 0) {
        memmove(x->d + 1, x->d, (size_t)x->n++);
        x->d[0] = (char)('0' + carry % 10);
        carry /= 10;
    }
}

// The midpoint between d (positive, finite) and the next double up, as an
// exact decimal string, then the same with one more digit just above and
// with its last digit lowered (just below).
static void test_midpoint_of(double d) {
    uint64_t b = dbits(d);
    unsigned exp_field = (unsigned)(b >> 52);
    uint64_t m = b & ((1ull << 52) - 1);
    int e = (int)exp_field - 1075;
    if (exp_field != 0) {
        m |= 1ull << 52;
    } else {
        e = -1074;
    }
    // midpoint = (2m + 1) * 2^(e - 1)
    struct digits x;
    digits_set(&x, 2 * m + 1);
    int e2 = e - 1, scale = 0;  // value = digits * 10^-scale
    if (e2 >= 0) {
        while (e2-- > 0) {
            digits_mul(&x, 2);
        }
    } else {
        scale = -e2;
        for (int i = 0; i < scale; i++) {
            digits_mul(&x, 5);
        }
    }
    char s[1300];
    int n = snprintf(s, sizeof s, "%.*se-%d", x.n, x.d, scale);
    compare(s);
    // Just above: an extra 1 digit (one more place of scale).
    snprintf(s, sizeof s, "%.*s1e-%d", x.n, x.d, scale + 1);
    compare(s);
    // Just below: the last digit (odd, 5 or the last of 2^k) minus one, then 9s.
    snprintf(s, sizeof s, "%.*s", x.n, x.d);
    s[x.n - 1]--;
    snprintf(s + x.n, sizeof s - (size_t)x.n, "999e-%d", scale + 3);
    compare(s);
    (void)n;
}

static void test_midpoints(void) {
    static const double fixed[] = {1.0, 0.1, 1e23, 9007199254740992.0, DBL_MAX, DBL_MIN,
                                   DBL_TRUE_MIN, 2 * DBL_TRUE_MIN, 0.5, 1e-300, 1e300};
    for (size_t i = 0; i < sizeof fixed / sizeof fixed[0]; i++) {
        if (fixed[i] != DBL_MAX) {
            test_midpoint_of(fixed[i]);
        }
    }
    for (int i = 0; i < 600; i++) {
        double d = from_dbits(next_random() & 0x7fefffffffffffffull);
        if (i % 4 == 0) {
            d = from_dbits(next_random() & 0x000fffffffffffffull);  // a subnormal
        }
        test_midpoint_of(d);
    }
}

static void test_special_forms(void) {
    char *end;
    const char *s = "infinite";
    double d = kora_strtod(s, &end);
    CHECK(d > DBL_MAX && end == s + 3, "infinite stops after inf");
    s = "-nan(xyz)rest";
    d = kora_strtod(s, &end);
    CHECK(d != d && (dbits(d) >> 63) && end == s + 9, "-nan(...) is a negative NaN");
    s = "nan(bad";
    d = kora_strtod(s, &end);
    CHECK(d != d && end == s + 3, "an unclosed nan( stops after nan");
    s = "nan(x y)";
    d = kora_strtod(s, &end);
    CHECK(d != d && end == s + 3, "nan( with a space is not an n-char-sequence");
    s = "0x1.8p1";
    CHECK(kora_strtod(s, &end) == 3.0 && *end == '\0', "hex float");
    s = "0xz";
    CHECK(kora_strtod(s, &end) == 0.0 && end == s + 1, "0x without digits is 0 then x");
    s = "  junk";
    CHECK(kora_strtod(s, &end) == 0.0 && end == s, "no number: end at the start");
    CHECK((dbits(kora_strtod("-0", NULL)) >> 63) == 1, "-0 keeps its sign");
    CHECK(kora_atof("2.5e3") == 2500.0, "atof");
}

static void test_errno(void) {
    errno = 0;
    CHECK(kora_strtod("1e400", NULL) > DBL_MAX && errno == ERANGE, "overflow: inf, ERANGE");
    errno = 0;
    CHECK(kora_strtod("-1e400", NULL) < -DBL_MAX && errno == ERANGE, "negative overflow");
    errno = 0;
    CHECK(kora_strtod("1e-400", NULL) == 0.0 && errno == ERANGE, "underflow to zero: ERANGE");
    errno = 0;
    CHECK(kora_strtof("1e39", NULL) > FLT_MAX && errno == ERANGE, "strtof overflow");
    errno = 0;
    CHECK(kora_strtod("1.5", NULL) == 1.5 && kora_strtod("0", NULL) == 0.0 &&
              kora_strtod("0x1p-1022", NULL) == DBL_MIN && errno == 0,
          "exact results leave errno alone");
}

static void test_long_inputs(void) {
    // More digits than are kept (800): the rest only counts as sticky.
    static char s[3000];
    memset(s, '0', 2000);
    s[0] = '1';
    s[1] = '.';
    s[2000] = '\0';
    compare(s);  // exactly 1
    s[1999] = '1';
    compare(s);  // a hair above 1
    memcpy(s, "9.", 2);
    memset(s + 2, '9', 1997);
    s[1999] = '\0';
    compare(s);  // a hair below 10
    memset(s, '0', 1500);
    s[0] = '.';
    s[1499] = '1';
    s[1500] = '\0';
    compare(s);  // 1e-1499: zero
}

TEST_MAIN(test_hand_picked, test_random_decimals, test_round_trips, test_midpoints,
          test_special_forms, test_errno, test_long_inputs)
