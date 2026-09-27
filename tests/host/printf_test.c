// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the kernel's tinyprintf (src/lib/printf.c), through
// tfp_sprintf. The 64-bit cases are the regressions: %lx of a value >= 2^32
// used to print no digits at all, and %lu/%ld of a large value overflowed the
// formatting buffer.

#include "test.h"

// Not via lib/printf.h: it #defines printf, which test.h uses for reporting.
void tfp_sprintf(char *s, char *fmt, ...);

static char out[64];

#define FMT_IS(expected, ...)                                            \
    do {                                                                 \
        tfp_sprintf(out, __VA_ARGS__);                                   \
        CHECK(strcmp(out, expected) == 0, "%s: got \"%s\", want \"%s\"", \
              #__VA_ARGS__, out, expected);                              \
    } while (0)

static void test_32bit(void) {
    FMT_IS("0", "%u", 0u);
    FMT_IS("4294967295", "%u", 4294967295u);
    FMT_IS("-1", "%d", -1);
    FMT_IS("-2147483648", "%d", (int)(-2147483647 - 1));
    FMT_IS("deadbeef DEADBEEF", "%x %X", 0xdeadbeefu, 0xdeadbeefu);
    FMT_IS("0", "%x", 0u);
}

static void test_64bit(void) {
    // The PCIe DMA bus address on a 4 GB Pi 4: printed as "0x" before.
    FMT_IS("400000000", "%lx", 0x400000000UL);
    FMT_IS("100000000", "%lx", 0x100000000UL);
    FMT_IS("ffffffffffffffff", "%lx", 0xffffffffffffffffUL);
    FMT_IS("C0000000", "%lX", 0xc0000000UL);
    FMT_IS("18446744073709551615", "%lu", 18446744073709551615UL);
    FMT_IS("4294967296", "%lu", 4294967296UL);
    FMT_IS("-9223372036854775808", "%ld", (long)(-9223372036854775807L - 1));
    FMT_IS("-4294967296", "%ld", -4294967296L);
    FMT_IS("0", "%lu", 0UL);
}

static void test_width_and_misc(void) {
    FMT_IS("0000002a", "%08x", 0x2au);
    FMT_IS("   42", "%5u", 42u);
    FMT_IS("0000000400000000", "%016lx", 0x400000000UL);
    FMT_IS("a-b", "%c-%s", 'a', "b");
    FMT_IS("100%", "100%%");
}

TEST_MAIN(test_32bit, test_64bit, test_width_and_misc)
