// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal host-side test support for the libc tests. The libc headers are on
// the include path instead of the host's, so the host's printf is declared by
// hand rather than pulled in from <stdio.h>.
#ifndef LIBC_TEST_H
#define LIBC_TEST_H

#include <stddef.h>

int printf(const char *fmt, ...);
int fflush(void *stream);

extern int test_failures;
extern int test_checks;

#define CHECK(cond, ...)                                \
    do {                                                \
        test_checks++;                                  \
        if (!(cond)) {                                  \
            test_failures++;                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)

// Use once per test program: TEST_MAIN(test_a, test_b, ...).
#define TEST_MAIN(...)                                                    \
    int test_failures;                                                    \
    int test_checks;                                                      \
    int main(void) {                                                      \
        void (*tests[])(void) = {__VA_ARGS__};                            \
        for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) { \
            tests[i]();                                                   \
        }                                                                 \
        printf("%s: %d checks, %d failures\n", __FILE__, test_checks,     \
               test_failures);                                            \
        fflush(NULL);                                                     \
        return test_failures != 0;                                        \
    }

#endif // LIBC_TEST_H
