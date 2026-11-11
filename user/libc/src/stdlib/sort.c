// SPDX-License-Identifier: GPL-3.0-or-later
//
// qsort and bsearch (C11 7.22.5). qsort is a heapsort, with insertion sort for
// short arrays: O(n log n) in the worst case, no recursion and no buffer, so
// it runs in constant stack space (a program's main thread has a single 4 KB
// stack page). It is not stable, which the standard does not require.

#include <stdlib.h>

typedef int (*compare_fn)(const void *, const void *);

// Arrays up to this many elements use insertion sort.
#define INSERTION_SORT_MAX 12

static void swap_bytes(unsigned char *a, unsigned char *b, size_t size) {
    // Word at a time when both elements allow it (the common case of ints,
    // pointers and structs of them), bytes otherwise.
    if (((size | (size_t)a | (size_t)b) & (sizeof(unsigned long) - 1)) == 0) {
        unsigned long *wa = (unsigned long *)a, *wb = (unsigned long *)b;
        for (size_t i = 0; i < size / sizeof(unsigned long); i++) {
            unsigned long t = wa[i];
            wa[i] = wb[i];
            wb[i] = t;
        }
        return;
    }
    for (size_t i = 0; i < size; i++) {
        unsigned char t = a[i];
        a[i] = b[i];
        b[i] = t;
    }
}

static void insertion_sort(unsigned char *base, size_t nmemb, size_t size, compare_fn compar) {
    for (size_t i = 1; i < nmemb; i++) {
        for (size_t j = i; j > 0; j--) {
            unsigned char *cur = base + j * size;
            unsigned char *prev = cur - size;
            if (compar(prev, cur) <= 0) {
                break;
            }
            swap_bytes(prev, cur, size);
        }
    }
}

// Restore the max-heap property below `root` in a heap of `count` elements.
static void sift_down(unsigned char *base, size_t root, size_t count, size_t size,
                      compare_fn compar) {
    for (;;) {
        size_t child = 2 * root + 1;
        if (child >= count) {
            return;
        }
        if (child + 1 < count && compar(base + child * size, base + (child + 1) * size) < 0) {
            child++;
        }
        if (compar(base + root * size, base + child * size) >= 0) {
            return;
        }
        swap_bytes(base + root * size, base + child * size, size);
        root = child;
    }
}

void qsort(void *base, size_t nmemb, size_t size, compare_fn compar) {
    unsigned char *b = base;
    if (nmemb < 2 || size == 0) {
        return;
    }
    if (nmemb <= INSERTION_SORT_MAX) {
        insertion_sort(b, nmemb, size, compar);
        return;
    }
    for (size_t i = nmemb / 2; i-- > 0;) {
        sift_down(b, i, nmemb, size, compar);
    }
    for (size_t end = nmemb - 1; end > 0; end--) {
        swap_bytes(b, b + end * size, size);
        sift_down(b, 0, end, size, compar);
    }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              compare_fn compar) {
    const unsigned char *b = base;
    size_t lo = 0, hi = nmemb;  // the key, if present, is in [lo, hi)
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *elem = b + mid * size;
        int c = compar(key, elem);
        if (c == 0) {
            return (void *)elem;
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return NULL;
}
