// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for src/stdlib/exit.c: exit(), _Exit() and abort() end the program
// through SYS_EXIT with the right status. The fake kernel (tests/fake_syscall.c)
// turns that syscall into a jump back here.

#include "test.h"

#include <stdlib.h>

#include "fake_syscall.h"

static fake_jmp_buf env;

#define EXPECT_EXIT(call, want, what)       \
    do {                                    \
        if (setjmp(&env) == 0) {            \
            fake_arm_exit(&env);            \
            call;                           \
            CHECK(0, "%s returned", what);  \
        } else {                            \
            CHECK(fake_exit_status() == (want), "%s: status %d, want %d", what, \
                  fake_exit_status(), (want)); \
        }                                   \
    } while (0)

static void test_exit(void) {
    EXPECT_EXIT(exit(0), 0, "exit(0)");
    EXPECT_EXIT(exit(1), 1, "exit(1)");
    EXPECT_EXIT(exit(42), 42, "exit(42)");
    EXPECT_EXIT(exit(-9), -9, "exit(-9)");
}

static void test_underscore_exit(void) {
    EXPECT_EXIT(_Exit(0), 0, "_Exit(0)");
    EXPECT_EXIT(_Exit(7), 7, "_Exit(7)");
}

static void test_abort(void) {
    EXPECT_EXIT(abort(), -6, "abort()");
}

TEST_MAIN(test_exit, test_underscore_exit, test_abort)
