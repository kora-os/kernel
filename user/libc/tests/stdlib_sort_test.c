// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for qsort and bsearch (src/stdlib/sort.c): many sizes (both sides
// of the insertion sort cut-off) and input shapes, element sizes from 1 byte
// to odd-sized and word-aligned structs, the result checked as sorted and as a
// permutation of the input, a comparison-count bound, and bsearch on every
// position plus misses.

#include "test.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static unsigned long long rng = 0x9e3779b97f4a7c15ull;

static unsigned next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (unsigned)(rng >> 32);
}

static unsigned long compares;

static int cmp_int(const void *a, const void *b) {
    compares++;
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int cmp_uchar(const void *a, const void *b) {
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

// 3 bytes: forces the byte-wise swap.
struct triple {
    unsigned char key, tag, pad;
};

static int cmp_triple(const void *a, const void *b) {
    return ((const struct triple *)a)->key - ((const struct triple *)b)->key;
}

// 24 bytes, word aligned: the word-wise swap moves the payload intact.
struct record {
    long key;
    long check;  // always ~key, so a torn swap shows up
    const char *name;
};

static int cmp_record(const void *a, const void *b) {
    long x = ((const struct record *)a)->key, y = ((const struct record *)b)->key;
    return (x > y) - (x < y);
}

// Compares longs that may sit at any address.
static int cmp_unaligned_long(const void *a, const void *b) {
    long x, y;
    memcpy(&x, a, sizeof x);
    memcpy(&y, b, sizeof y);
    return (x > y) - (x < y);
}

enum shape { RANDOM, SORTED, REVERSED, EQUAL, FEW_VALUES, ORGAN_PIPE };

static void fill(int *a, size_t n, enum shape shape) {
    for (size_t i = 0; i < n; i++) {
        switch (shape) {
        case RANDOM: a[i] = (int)(next_random() - (next_random() >> 1)); break;
        case SORTED: a[i] = (int)i; break;
        case REVERSED: a[i] = (int)(n - i); break;
        case EQUAL: a[i] = 7; break;
        case FEW_VALUES: a[i] = (int)(next_random() % 4); break;
        case ORGAN_PIPE: a[i] = (int)(i < n / 2 ? i : n - i); break;
        }
    }
}

// Sorted, and the same multiset as the input (sum and xor of hashes).
static int check_ints(const int *in, const int *out, size_t n) {
    unsigned long long sum_in = 0, sum_out = 0, x_in = 0, x_out = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && out[i - 1] > out[i]) {
            return 0;
        }
        unsigned long long hi = (unsigned)in[i] * 0x9e3779b97f4a7c15ull;
        unsigned long long ho = (unsigned)out[i] * 0x9e3779b97f4a7c15ull;
        sum_in += hi;
        sum_out += ho;
        x_in ^= hi >> 7;
        x_out ^= ho >> 7;
    }
    return sum_in == sum_out && x_in == x_out;
}

static int input[2000], output[2000];

static void test_qsort_ints(void) {
    static const size_t sizes[] = {0, 1, 2, 3, 5, 11, 12, 13, 14, 31, 64, 100, 257, 1000, 2000};
    for (int shape = RANDOM; shape <= ORGAN_PIPE; shape++) {
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            size_t n = sizes[s];
            fill(input, n, (enum shape)shape);
            memcpy(output, input, n * sizeof(int));
            compares = 0;
            qsort(output, n, sizeof(int), cmp_int);
            CHECK(check_ints(input, output, n), "shape %d, n %zu: sorted permutation", shape, n);
            // Heapsort makes at most about 2 n log2 n comparisons.
            unsigned long log2n = 1;
            while ((1ul << log2n) < n) {
                log2n++;
            }
            CHECK(compares <= 2 * n * log2n + n, "shape %d, n %zu: %lu comparisons", shape, n,
                  compares);
        }
    }
}

static void test_qsort_element_sizes(void) {
    unsigned char bytes[300];
    for (size_t i = 0; i < sizeof bytes; i++) {
        bytes[i] = (unsigned char)next_random();
    }
    qsort(bytes, sizeof bytes, 1, cmp_uchar);
    int ok = 1;
    for (size_t i = 1; i < sizeof bytes; i++) {
        ok &= bytes[i - 1] <= bytes[i];
    }
    CHECK(ok, "1-byte elements");

    struct triple t[200];
    for (size_t i = 0; i < 200; i++) {
        t[i].key = (unsigned char)next_random();
        t[i].tag = (unsigned char)(t[i].key ^ 0x5a);
        t[i].pad = 0xee;
    }
    qsort(t, 200, sizeof t[0], cmp_triple);
    ok = 1;
    for (size_t i = 0; i < 200; i++) {
        ok &= (i == 0 || t[i - 1].key <= t[i].key) && t[i].tag == (t[i].key ^ 0x5a) &&
              t[i].pad == 0xee;
    }
    CHECK(ok, "3-byte elements stay whole");

    static struct record r[500];
    for (size_t i = 0; i < 500; i++) {
        r[i].key = (long)next_random() - (long)next_random();
        r[i].check = ~r[i].key;
        r[i].name = (i & 1) ? "odd" : "even";
    }
    qsort(r, 500, sizeof r[0], cmp_record);
    ok = 1;
    for (size_t i = 0; i < 500; i++) {
        ok &= (i == 0 || r[i - 1].key <= r[i].key) && r[i].check == ~r[i].key &&
              (strcmp(r[i].name, "odd") == 0 || strcmp(r[i].name, "even") == 0);
    }
    CHECK(ok, "24-byte records stay whole");

    // An unaligned base with word-sized elements falls back to byte swaps.
    static unsigned char raw[8 * 100 + 1];
    long values[100];
    for (size_t i = 0; i < 100; i++) {
        values[i] = (long)(100 - i) * 1000003;
    }
    memcpy(raw + 1, values, sizeof values);
    qsort(raw + 1, 100, sizeof(long), cmp_unaligned_long);
    memcpy(values, raw + 1, sizeof values);
    ok = 1;
    for (size_t i = 0; i < 100; i++) {
        ok &= values[i] == (long)(i + 1) * 1000003;
    }
    CHECK(ok, "unaligned word-sized elements");

    int one = 5;
    qsort(&one, 1, sizeof one, cmp_int);
    qsort(NULL, 0, sizeof one, cmp_int);
    qsort(&one, 1, 0, cmp_int);
    CHECK(one == 5, "trivial sorts leave the data alone");
}

static void test_bsearch(void) {
    int a[100];
    for (int i = 0; i < 100; i++) {
        a[i] = i * 2;  // even numbers 0..198
    }
    int ok = 1;
    for (int i = 0; i < 100; i++) {
        int key = i * 2;
        int *hit = bsearch(&key, a, 100, sizeof a[0], cmp_int);
        ok &= hit == &a[i];
        key = i * 2 + 1;
        ok &= bsearch(&key, a, 100, sizeof a[0], cmp_int) == NULL;
    }
    CHECK(ok, "bsearch finds every element and no odd number");
    int key = -1;
    CHECK(bsearch(&key, a, 100, sizeof a[0], cmp_int) == NULL, "below the range");
    key = 1000;
    CHECK(bsearch(&key, a, 100, sizeof a[0], cmp_int) == NULL, "above the range");
    key = 0;
    CHECK(bsearch(&key, a, 0, sizeof a[0], cmp_int) == NULL, "empty array");
    CHECK(bsearch(&key, a, 1, sizeof a[0], cmp_int) == &a[0], "one element");

    int dup[] = {1, 3, 3, 3, 3, 9};
    key = 3;
    int *hit = bsearch(&key, dup, 6, sizeof dup[0], cmp_int);
    CHECK(hit != NULL && *hit == 3, "bsearch finds one of several equal elements");
}

TEST_MAIN(test_qsort_ints, test_qsort_element_sizes, test_bsearch)
