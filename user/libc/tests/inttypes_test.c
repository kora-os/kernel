// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for <inttypes.h> (src/inttypes/inttypes.c): every PRI and SCN
// macro through the libc's own snprintf and sscanf at the limits of its type
// (the compiler's format checking, -Wformat -Werror, also checks each macro
// against its type, here and for AArch64 in libc_header_check), imaxabs and
// imaxdiv, strtoimax and strtoumax, and the wide conversions against the
// narrow ones on hand-picked and random strings.

#include "test.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define FORMATS(type, pri, scn, value, want)                                         \
    do {                                                                             \
        char buf[64];                                                                \
        type v = (type)(value), back = 0;                                            \
        snprintf(buf, sizeof buf, "%" pri, v);                                       \
        CHECK(strcmp(buf, want) == 0, "%%%s of %s: \"%s\", want \"%s\"", pri, #value, buf, want); \
        CHECK(sscanf(buf, "%" scn, &back) == 1 && back == v, "%%%s reads it back", scn);         \
    } while (0)

static void test_format_macros(void) {
    FORMATS(int8_t, PRId8, SCNd8, INT8_MIN, "-128");
    FORMATS(int8_t, PRIi8, SCNi8, INT8_MAX, "127");
    FORMATS(uint8_t, PRIu8, SCNu8, UINT8_MAX, "255");
    FORMATS(uint8_t, PRIo8, SCNo8, UINT8_MAX, "377");
    FORMATS(uint8_t, PRIx8, SCNx8, 0xab, "ab");
    FORMATS(int16_t, PRId16, SCNd16, INT16_MIN, "-32768");
    FORMATS(uint16_t, PRIu16, SCNu16, UINT16_MAX, "65535");
    FORMATS(uint16_t, PRIX16, SCNx16, 0xbeef, "BEEF");
    FORMATS(int32_t, PRId32, SCNd32, INT32_MIN, "-2147483648");
    FORMATS(uint32_t, PRIu32, SCNu32, UINT32_MAX, "4294967295");
    FORMATS(uint32_t, PRIo32, SCNo32, UINT32_MAX, "37777777777");
    FORMATS(int64_t, PRId64, SCNd64, INT64_MIN, "-9223372036854775808");
    FORMATS(int64_t, PRIi64, SCNi64, INT64_MAX, "9223372036854775807");
    FORMATS(uint64_t, PRIu64, SCNu64, UINT64_MAX, "18446744073709551615");
    FORMATS(uint64_t, PRIx64, SCNx64, 0x123456789abcdef0u, "123456789abcdef0");
    FORMATS(uint64_t, PRIX64, SCNx64, 0x123456789abcdef0u, "123456789ABCDEF0");
    FORMATS(int_least8_t, PRIdLEAST8, SCNdLEAST8, -5, "-5");
    FORMATS(uint_least16_t, PRIuLEAST16, SCNuLEAST16, UINT_LEAST16_MAX, "65535");
    FORMATS(int_least32_t, PRIiLEAST32, SCNiLEAST32, INT_LEAST32_MIN, "-2147483648");
    FORMATS(uint_least64_t, PRIxLEAST64, SCNxLEAST64, UINT_LEAST64_MAX, "ffffffffffffffff");
    FORMATS(int_fast8_t, PRIdFAST8, SCNdFAST8, INT_FAST8_MIN, "-128");
    FORMATS(uint_fast16_t, PRIuFAST16, SCNuFAST16, 1234, "1234");
    FORMATS(int_fast32_t, PRIdFAST32, SCNdFAST32, -77, "-77");
    FORMATS(uint_fast64_t, PRIoFAST64, SCNoFAST64, 8, "10");
    FORMATS(intmax_t, PRIdMAX, SCNdMAX, INTMAX_MIN, "-9223372036854775808");
    FORMATS(uintmax_t, PRIuMAX, SCNuMAX, UINTMAX_MAX, "18446744073709551615");
    FORMATS(uintmax_t, PRIXMAX, SCNxMAX, 255, "FF");
    FORMATS(intptr_t, PRIdPTR, SCNdPTR, -1, "-1");
    FORMATS(uintptr_t, PRIxPTR, SCNxPTR, 0xdeadbeef, "deadbeef");
    // Every macro is a string literal that can be pasted into a format.
    const char *all[] = {PRIdFAST64, PRIiFAST16, PRIoLEAST8, PRIuPTR, PRIXLEAST32, PRIxFAST8,
                         SCNiMAX, SCNoPTR, SCNuFAST8, SCNxLEAST16, SCNdLEAST64, SCNiFAST64};
    int ok = 1;
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        ok &= strlen(all[i]) >= 1 && strlen(all[i]) <= 3;
    }
    CHECK(ok, "the macros are short conversion specifiers");
}

static void test_functions(void) {
    CHECK(imaxabs(-5) == 5 && imaxabs(5) == 5 && imaxabs(INTMAX_MIN + 1) == INTMAX_MAX, "imaxabs");
    imaxdiv_t d = imaxdiv(-7, 2);
    CHECK(d.quot == -3 && d.rem == -1, "imaxdiv truncates towards zero");
    d = imaxdiv(INTMAX_MAX, 10);
    CHECK(d.quot == INTMAX_MAX / 10 && d.rem == 7, "imaxdiv at the limit");
    char *end;
    errno = 0;
    CHECK(strtoimax("-9223372036854775808x", &end, 10) == INTMAX_MIN && *end == 'x' && errno == 0,
          "strtoimax at its minimum");
    CHECK(strtoimax("9223372036854775808", NULL, 10) == INTMAX_MAX && errno == ERANGE,
          "strtoimax clamps with ERANGE");
    errno = 0;
    CHECK(strtoumax("0xffffffffffffffff", &end, 0) == UINTMAX_MAX && *end == '\0' && errno == 0,
          "strtoumax");
    CHECK(strtoumax("-1", NULL, 10) == UINTMAX_MAX, "strtoumax negates in the unsigned type");
}

// The wide conversions must agree with the narrow ones on ASCII text.
static void compare_wide(const char *s, int base) {
    wchar_t w[80];
    size_t n = strlen(s);
    for (size_t i = 0; i <= n; i++) {
        w[i] = (wchar_t)(unsigned char)s[i];
    }
    char *nend;
    wchar_t *wend;
    errno = 0;
    intmax_t want = strtoimax(s, &nend, base);
    int want_errno = errno;
    errno = 0;
    intmax_t got = wcstoimax(w, &wend, base);
    CHECK(got == want && wend - w == nend - s && errno == want_errno,
          "wcstoimax(\"%s\", %d): %jd at %td, want %jd at %td", s, base, got, wend - w, want,
          nend - s);
    errno = 0;
    uintmax_t uwant = strtoumax(s, &nend, base);
    want_errno = errno;
    errno = 0;
    uintmax_t ugot = wcstoumax(w, &wend, base);
    CHECK(ugot == uwant && wend - w == nend - s && errno == want_errno,
          "wcstoumax(\"%s\", %d)", s, base);
}

static unsigned long long rng = 0x6a09e667f3bcc909ull;

static unsigned long long next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static void test_wide(void) {
    static const char *const cases[] = {
        "0", "-0", "+42", "  \t\n-17abc", "0x1F", "0X", "0xg", "017", "08", "zz", "-",
        "9223372036854775807", "9223372036854775808", "-9223372036854775808",
        "-9223372036854775809", "18446744073709551615", "18446744073709551616", "", "   ",
        "+-1", "1z", "777777777777777777777777", "-0x8000000000000000",
    };
    static const int bases[] = {0, 2, 8, 10, 16, 36, 1, -1, 37};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (size_t b = 0; b < sizeof bases / sizeof bases[0]; b++) {
            compare_wide(cases[i], bases[b]);
        }
    }
    char s[48];
    for (int i = 0; i < 20000; i++) {
        size_t n = next_random() % 30;
        for (size_t k = 0; k < n; k++) {
            s[k] = " \t+-0123456789abcxyzXZ"[next_random() % 22];
        }
        s[n] = '\0';
        compare_wide(s, (int)(next_random() % 37));
    }
    // A wide character outside ASCII is never a digit or white space.
    wchar_t w[] = {0x0663, L'1', 0};  // ARABIC-INDIC DIGIT THREE
    wchar_t *end;
    CHECK(wcstoimax(w, &end, 10) == 0 && end == w, "a non-ASCII digit is not a digit");
    wchar_t space[] = {0x3000, L'5', 0};  // IDEOGRAPHIC SPACE
    CHECK(wcstoumax(space, &end, 10) == 0 && end == space, "nor is non-ASCII white space");
}

TEST_MAIN(test_format_macros, test_functions, test_wide)
