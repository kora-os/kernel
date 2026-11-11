// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the small parts of <stdlib.h>: abs and div families
// (src/stdlib/integer.c), rand/srand (src/stdlib/rand.c) and getenv.

#include "test.h"

#include <limits.h>
#include <stdlib.h>

static void test_abs(void) {
    CHECK(abs(0) == 0 && abs(5) == 5 && abs(-5) == 5, "abs");
    CHECK(abs(INT_MAX) == INT_MAX && abs(-INT_MAX) == INT_MAX, "abs at the limits");
    CHECK(abs(INT_MIN) == INT_MIN, "abs(INT_MIN) wraps without UB");
    CHECK(labs(-123456789012L) == 123456789012L && labs(LONG_MIN) == LONG_MIN, "labs");
    CHECK(llabs(-1LL) == 1 && llabs(LLONG_MIN) == LLONG_MIN, "llabs");
}

static void test_div(void) {
    div_t d = div(7, 2);
    CHECK(d.quot == 3 && d.rem == 1, "div(7, 2)");
    d = div(-7, 2);
    CHECK(d.quot == -3 && d.rem == -1, "div truncates towards zero");
    d = div(7, -2);
    CHECK(d.quot == -3 && d.rem == 1, "div with a negative divisor");
    d = div(INT_MIN, 1);
    CHECK(d.quot == INT_MIN && d.rem == 0, "div(INT_MIN, 1)");
    ldiv_t l = ldiv(-100000000001L, 10);
    CHECK(l.quot == -10000000000L && l.rem == -1, "ldiv");
    lldiv_t ll = lldiv(LLONG_MAX, 1000000007LL);
    CHECK(ll.quot * 1000000007LL + ll.rem == LLONG_MAX && ll.rem >= 0, "lldiv identity");
}

static void test_rand(void) {
    int first[8];
    for (int i = 0; i < 8; i++) {
        first[i] = rand();
    }
    srand(1);
    int same = 1;
    for (int i = 0; i < 8; i++) {
        same &= rand() == first[i];
    }
    CHECK(same, "rand before srand is the srand(1) sequence");

    srand(42);
    int a = rand(), b = rand();
    srand(42);
    CHECK(rand() == a && rand() == b, "srand repeats a sequence");
    srand(43);
    CHECK(rand() != a, "a different seed gives a different sequence");

    // Range, and a rough uniformity check: each of 16 buckets of the top bits
    // gets 1/16 of 64000 draws within 15%.
    int buckets[16] = {0};
    int in_range = 1;
    srand(7);
    for (int i = 0; i < 64000; i++) {
        int r = rand();
        in_range &= r >= 0 && r <= RAND_MAX;
        buckets[r >> 27]++;
    }
    CHECK(in_range, "0 <= rand() <= RAND_MAX");
    for (int i = 0; i < 16; i++) {
        CHECK(buckets[i] > 3400 && buckets[i] < 4600, "bucket %d: %d", i, buckets[i]);
    }
    // The low bit alternates in a plain LCG; here it must not.
    int alternations = 0, prev = rand() & 1;
    for (int i = 0; i < 1000; i++) {
        int bit = rand() & 1;
        alternations += bit != prev;
        prev = bit;
    }
    CHECK(alternations > 400 && alternations < 600, "low bit is not periodic (%d)", alternations);
    CHECK(RAND_MAX >= 32767, "RAND_MAX meets the minimum");
}

static void test_getenv(void) {
    CHECK(getenv("PATH") == NULL && getenv("") == NULL, "no environment yet");
}

static void test_constants(void) {
    CHECK(EXIT_SUCCESS == 0 && EXIT_FAILURE != 0, "exit status macros");
}

TEST_MAIN(test_abs, test_div, test_rand, test_getenv, test_constants)
