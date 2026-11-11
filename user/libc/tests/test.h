// SPDX-License-Identifier: GPL-3.0-or-later
//
// Minimal host-side test support for the libc tests. The libc headers are on
// the include path instead of the host's, and host_rename.h renames the
// libc's own printf and fflush (kora_printf, ...), so the host's are bound by
// symbol name here: test_printf and test_fflush always reach the host libc,
// whatever the code under test does with its streams.
#ifndef LIBC_TEST_H
#define LIBC_TEST_H

#include <stddef.h>

#define TEST_STRINGIFY2(x) #x
#define TEST_STRINGIFY(x) TEST_STRINGIFY2(x)
// The host's symbol `name` (with the platform's prefix: _printf on macOS).
#define TEST_HOST_SYMBOL(name) __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) #name)

int test_printf(const char *fmt, ...) TEST_HOST_SYMBOL(printf);
int test_fflush(void *stream) TEST_HOST_SYMBOL(fflush);

extern int test_failures;
extern int test_checks;

#define CHECK(cond, ...)                                \
    do {                                                \
        test_checks++;                                  \
        if (!(cond)) {                                  \
            test_failures++;                            \
            test_printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            test_printf(__VA_ARGS__);                   \
            test_printf("\n");                          \
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
        test_printf("%s: %d checks, %d failures\n", __FILE__, test_checks, \
               test_failures);                                            \
        test_fflush(NULL);                                                \
        return test_failures != 0;                                        \
    }

#endif // LIBC_TEST_H
