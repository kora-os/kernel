// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal host-side test support. Host tests compile pure-logic kernel sources
// with the host compiler (plus sanitizers) and check their behaviour, without
// QEMU or hardware. See tests/README.md.
//
// Kernel headers (common.h) define their own fixed-width types, which clash
// with the host's <stdint.h>/<stdio.h>, so tests declare the few libc functions
// they need here instead of including system headers.
#pragma once

#include "common.h"

int printf(const char *fmt, ...);
void *calloc(size_t n, size_t size);
int strcmp(const char *a, const char *b);

extern int test_failures;
extern int test_checks;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        test_checks++;                                                \
        if (!(cond)) {                                                \
            test_failures++;                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

// Define the counters and a main() that runs the listed test functions and
// reports. Use once per test program: TEST_MAIN(test_a, test_b, ...).
#define TEST_MAIN(...)                                                \
    int test_failures;                                                \
    int test_checks;                                                  \
    int main(void) {                                                  \
        void (*tests[])(void) = {__VA_ARGS__};                        \
        for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) { \
            tests[i]();                                               \
        }                                                             \
        printf("%s: %d checks, %d failures\n", __FILE__, test_checks, \
               test_failures);                                        \
        return test_failures != 0;                                    \
    }
