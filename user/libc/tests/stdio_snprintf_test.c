// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the snprintf family (src/stdio/snprintf.c, src/stdio/format.c).
//
// Most checks are differential: every combination of flags, field width and
// precision (literal and *), length modifier and edge-case value for the
// integer, character and string conversions is formatted by the libc under
// test and by the host's own snprintf, and the two must agree byte for byte,
// return value included. Only combinations the standard defines are compared.
// The rest (choices the standard leaves open, truncation, %n, UTF-8 for %lc
// and %ls, errors) are checked by hand.

#include "test.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The host's vsnprintf, the reference. host_rename.h made the libc's own
// kora_vsnprintf, so after the #undef the plain name is the host's.
#undef vsnprintf
int vsnprintf(char *s, size_t n, const char *format, __builtin_va_list ap);

static unsigned mismatches;

static void compare(const char *fmt, ...) {
    __builtin_va_list a, b;
    __builtin_va_start(a, fmt);
    __builtin_va_copy(b, a);
    char want[128], got[128];
    int rw = vsnprintf(want, sizeof want, fmt, a);
    int rg = kora_vsnprintf(got, sizeof got, fmt, b);
    __builtin_va_end(b);
    __builtin_va_end(a);
    int same = rw == rg && strcmp(want, got) == 0;
    test_checks++;
    if (!same) {
        test_failures++;
        if (++mismatches <= 20) {  // enough to diagnose, not a flood
            printf("FAIL \"%s\": host \"%s\" (%d), libc \"%s\" (%d)\n", fmt, want, rw, got, rg);
        }
    }
}

enum length { NONE, HH, H, L, LL, J, Z, T, LENGTHS };
static const char *const length_text[LENGTHS] = {"", "hh", "h", "l", "ll", "j", "z", "t"};

// Calls compare() with the value passed as the type the length modifier asks
// for, after the * arguments the format uses.
#define PASS(type, v)                                    \
    do {                                                 \
        if (star_w && star_p)                            \
            compare(fmt, w, p, (type)(v));               \
        else if (star_w)                                 \
            compare(fmt, w, (type)(v));                  \
        else if (star_p)                                 \
            compare(fmt, p, (type)(v));                  \
        else                                             \
            compare(fmt, (type)(v));                     \
    } while (0)

static void run_signed(const char *fmt, int star_w, int w, int star_p, int p, enum length len,
                       intmax_t v) {
    switch (len) {
    case NONE: case HH: case H: PASS(int, v); break;
    case L: PASS(long, v); break;
    case LL: PASS(long long, v); break;
    case J: PASS(intmax_t, v); break;
    case Z: PASS(size_t, v); break;
    case T: PASS(ptrdiff_t, v); break;
    default: break;
    }
}

static void run_unsigned(const char *fmt, int star_w, int w, int star_p, int p, enum length len,
                         uintmax_t v) {
    switch (len) {
    case NONE: case HH: case H: PASS(unsigned int, v); break;
    case L: PASS(unsigned long, v); break;
    case LL: PASS(unsigned long long, v); break;
    case J: PASS(uintmax_t, v); break;
    case Z: PASS(size_t, v); break;
    case T: PASS(ptrdiff_t, v); break;
    default: break;
    }
}

// Widths and precisions: none, literal, and * with a few argument values
// (negative ones mean '-' for a width and "no precision" for a precision).
static const char *const widths[] = {"", "1", "4", "12", "*"};
static const int star_widths[] = {-7, 0, 9};
static const char *const precisions[] = {"", ".", ".0", ".1", ".3", ".12", ".*"};
static const int star_precisions[] = {-1, 0, 5};

static const intmax_t signed_values[] = {0, 1, -1, 7, -42, 127, -128, 255, 300, 32767,
                                         -32768, 65535, 70000, INT_MAX, INT_MIN, 4294967295LL,
                                         LONG_MAX, LONG_MIN};
static const uintmax_t unsigned_values[] = {0, 1, 7, 8, 64, 255, 256, 4095, 65535, 65536,
                                            UINT_MAX, 0x100000000ull, ULONG_MAX, 01234567};

static void sweep(const char *flag_chars, const char *convs, int is_signed) {
    size_t nflags = strlen(flag_chars);
    char flags[8], fmt[48];
    for (unsigned mask = 0; mask < (1u << nflags); mask++) {
        size_t nf = 0;
        for (size_t i = 0; i < nflags; i++) {
            if (mask & (1u << i)) {
                flags[nf++] = flag_chars[i];
            }
        }
        flags[nf] = '\0';
        for (size_t wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
            int star_w = widths[wi][0] == '*';
            for (size_t pi = 0; pi < sizeof precisions / sizeof precisions[0]; pi++) {
                int star_p = precisions[pi][0] != '\0' && precisions[pi][1] == '*';
                for (const char *c = convs; *c; c++) {
                    for (int len = NONE; len < LENGTHS; len++) {
                        snprintf(fmt, sizeof fmt, "%%%s%s%s%s%c", flags, widths[wi],
                                 precisions[pi], length_text[len], *c);
                        for (size_t swi = 0; swi < (star_w ? 3u : 1u); swi++) {
                            for (size_t spi = 0; spi < (star_p ? 3u : 1u); spi++) {
                                int w = star_widths[swi], p = star_precisions[spi];
                                if (is_signed) {
                                    for (size_t v = 0; v < sizeof signed_values / sizeof signed_values[0]; v++) {
                                        run_signed(fmt, star_w, w, star_p, p, (enum length)len,
                                                   signed_values[v]);
                                    }
                                } else {
                                    for (size_t v = 0; v < sizeof unsigned_values / sizeof unsigned_values[0]; v++) {
                                        run_unsigned(fmt, star_w, w, star_p, p, (enum length)len,
                                                     unsigned_values[v]);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

static void test_integers_against_host(void) {
    // '+' and ' ' apply to signed conversions, '#' to o, x and X: other
    // combinations are undefined or ignored, so they are not compared.
    sweep("-+ 0", "di", 1);
    sweep("-#0", "oxX", 0);
    sweep("-0", "u", 0);
}

static void test_strings_and_chars_against_host(void) {
    static const char *const strings[] = {"", "a", "hello", "a longer string than the width"};
    static const int chars[] = {'x', ' ', '%', 200};
    char fmt[32];
    for (int left = 0; left < 2; left++) {
        for (size_t wi = 0; wi < sizeof widths / sizeof widths[0]; wi++) {
            int star_w = widths[wi][0] == '*';
            for (size_t pi = 0; pi < sizeof precisions / sizeof precisions[0]; pi++) {
                int star_p = precisions[pi][0] != '\0' && precisions[pi][1] == '*';
                for (size_t swi = 0; swi < (star_w ? 3u : 1u); swi++) {
                    for (size_t spi = 0; spi < (star_p ? 3u : 1u); spi++) {
                        int w = star_widths[swi], p = star_precisions[spi];
                        snprintf(fmt, sizeof fmt, "[%%%s%s%ss]", left ? "-" : "", widths[wi],
                                 precisions[pi]);
                        for (size_t i = 0; i < sizeof strings / sizeof strings[0]; i++) {
                            PASS(const char *, strings[i]);
                        }
                        if (pi == 0) {  // a precision with %c is undefined
                            snprintf(fmt, sizeof fmt, "[%%%s%sc]", left ? "-" : "", widths[wi]);
                            for (size_t i = 0; i < sizeof chars / sizeof chars[0]; i++) {
                                PASS(int, chars[i]);
                            }
                        }
                    }
                }
            }
        }
    }
    compare("plain text, no conversions");
    compare("%d%% of %s", 50, "everything");
    compare("%c%c%c", 'a', 'b', 'c');
    compare("mixed %5d|%-8s|%#x|%c|%lld|%hhu", -12, "left", 255, 'Z', -9000000000LL, 511);
}

static int fmt_into(char *buf, size_t n, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = kora_vsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

static void test_open_choices(void) {
    char buf[64];
    CHECK(kora_snprintf(buf, sizeof buf, "%s", (char *)NULL) == 6 && strcmp(buf, "(null)") == 0,
          "null %%s prints (null): %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "[%.2s]", (char *)NULL) == 4 && strcmp(buf, "[(n]") == 0,
          "null %%s honours the precision: %s", buf);
    kora_snprintf(buf, sizeof buf, "%p", (void *)0);
    CHECK(strcmp(buf, "0x0") == 0, "%%p of NULL: %s", buf);
    kora_snprintf(buf, sizeof buf, "%p|%10p|%-8p|", (void *)0x1234, (void *)0xabc, (void *)0x1);
    CHECK(strcmp(buf, "0x1234|     0xabc|0x1     |") == 0, "%%p with widths: %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "%y and %") == 8 && strcmp(buf, "%y and %") == 0,
          "an unknown conversion and a trailing %% print as written: %s", buf);
    kora_snprintf(buf, sizeof buf, "%5%|%-5%");
    CHECK(strcmp(buf, "%|%") == 0, "%%%% ignores width: %s", buf);
    kora_snprintf(buf, sizeof buf, "%+u|% x", 5u, 10u);
    CHECK(strcmp(buf, "5|a") == 0, "+ and space ignored on unsigned conversions: %s", buf);
    // Floats are not implemented yet, but they must not unbalance the arguments.
    kora_snprintf(buf, sizeof buf, "%f|%d|%.3e|%Lg|%d", 1.5, 7, 2.0, (long double)3.0, 8);
    CHECK(strcmp(buf, "%f|7|%.3e|%Lg|8") == 0, "float placeholders keep the arguments: %s", buf);
}

static void test_truncation_and_return(void) {
    char buf[16];
    memset(buf, '#', sizeof buf);
    CHECK(kora_snprintf(buf, 5, "%s", "abcdefgh") == 8, "returns the full length");
    CHECK(strcmp(buf, "abcd") == 0 && buf[5] == '#', "stores n-1 characters and a NUL");
    CHECK(kora_snprintf(NULL, 0, "%d-%s", 1234, "xyz") == 8, "n = 0 measures without writing");
    memset(buf, '#', sizeof buf);
    CHECK(kora_snprintf(buf, 1, "abc") == 3 && buf[0] == '\0' && buf[1] == '#', "n = 1 stores the NUL");
    CHECK(kora_snprintf(buf, sizeof buf, "") == 0 && buf[0] == '\0', "empty format");
    memset(buf, '#', sizeof buf);
    CHECK(kora_snprintf(buf, 6, "%10d", 42) == 10 && strcmp(buf, "     ") == 0,
          "truncation inside padding: \"%s\"", buf);
    CHECK(fmt_into(buf, sizeof buf, "%s=%d", "v", 3) == 3 && strcmp(buf, "v=3") == 0, "vsnprintf");
    char big[32];
    CHECK(kora_sprintf(big, "%08.3f|%-4d|%x", 0.0, 12, 0xbeefu) == 16 &&
              strcmp(big, "%08.3f|12  |beef") == 0,
          "sprintf: %s", big);
    char c = 0;
    CHECK(kora_snprintf(buf, sizeof buf, "a%cb", c) == 3 && buf[1] == '\0' && buf[2] == 'b',
          "%%c writes a NUL byte");
}

static void test_count_stores(void) {
    char buf[64];
    int n = -1;
    signed char hh = -1;
    short h = -1;
    long l = -1;
    long long ll = -1;
    size_t z = 99;
    intmax_t j = -1;
    ptrdiff_t t = -1;
    kora_snprintf(buf, sizeof buf, "abc%nde%hhnf%hn%ln%lln%zn%jn%tnghij", &n, &hh, &h, &l, &ll, &z,
                  &j, &t);
    CHECK(n == 3 && hh == 5 && h == 6 && l == 6 && ll == 6 && z == 6 && j == 6 && t == 6,
          "%%n stores the count so far: %d %d %d %ld %lld %zu", n, hh, h, l, ll, z);
    CHECK(strcmp(buf, "abcdefghij") == 0, "%%n prints nothing: %s", buf);
    kora_snprintf(buf, 4, "%s%n", "truncated", &n);
    CHECK(n == 9, "%%n counts characters that did not fit (%d)", n);
}

static void test_utf8(void) {
    char buf[64];
    static const __WCHAR_TYPE__ word[] = {'h', 0xe9, 'l', 0x20ac, 0x1f600, 0};  // hé l€😀
    CHECK(kora_snprintf(buf, sizeof buf, "%ls", word) == 11 &&
              strcmp(buf, "h\xc3\xa9l\xe2\x82\xac\xf0\x9f\x98\x80") == 0,
          "%%ls writes UTF-8");
    CHECK(kora_snprintf(buf, sizeof buf, "[%.4ls]", word) == 6 && strcmp(buf, "[h\xc3\xa9l]") == 0,
          "%%ls precision counts bytes: %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "[%.5ls]", word) == 6 && strcmp(buf, "[h\xc3\xa9l]") == 0,
          "%%ls precision never splits a character: %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "[%6ls]", word + 3) == 9 &&
              strcmp(buf, "[\xe2\x82\xac\xf0\x9f\x98\x80]") == 0,
          "%%ls width counts bytes: %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "[%-3lc]", (__WINT_TYPE__)0xe9) == 5 &&
              strcmp(buf, "[\xc3\xa9 ]") == 0,
          "%%lc writes UTF-8: %s", buf);
    CHECK(kora_snprintf(buf, sizeof buf, "%ls", (__WCHAR_TYPE__ *)NULL) == 6, "null %%ls");

    errno = 0;
    static const __WCHAR_TYPE__ surrogate[] = {'a', 0xd800, 0};
    CHECK(kora_snprintf(buf, sizeof buf, "%ls", surrogate) == -1 && errno == EILSEQ,
          "a surrogate has no UTF-8 encoding: EILSEQ");
    errno = 0;
    CHECK(kora_snprintf(buf, sizeof buf, "%lc", (__WINT_TYPE__)0x110000) == -1 && errno == EILSEQ,
          "past U+10FFFF: EILSEQ");
}

static void test_overflow(void) {
    char buf[8];
    errno = 0;
    CHECK(kora_snprintf(buf, sizeof buf, "%2147483648d", 1) == -1 && errno == EOVERFLOW,
          "a width past INT_MAX: EOVERFLOW");
    errno = 0;
    CHECK(kora_snprintf(buf, sizeof buf, "%.2147483648d", 1) == -1 && errno == EOVERFLOW,
          "a precision past INT_MAX: EOVERFLOW");
    errno = 0;
    CHECK(kora_snprintf(buf, sizeof buf, "%*d", INT_MIN, 1) == -1 && errno == EOVERFLOW,
          "a * width of INT_MIN: EOVERFLOW");
}

TEST_MAIN(test_integers_against_host, test_strings_and_chars_against_host, test_open_choices,
          test_truncation_and_return, test_count_stores, test_utf8, test_overflow)
