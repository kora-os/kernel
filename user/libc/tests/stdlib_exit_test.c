// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for src/stdlib/exit.c: exit(), _Exit(), quick_exit() and abort()
// end the program through SYS_EXIT with the right status, and exit() and
// quick_exit() run their own handlers, last registered first. The fake kernel
// (tests/fake_syscall.c) turns that syscall into a jump back here.

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

static char trace[64];
static int trace_len;

static void note(char c) {
    if (trace_len < (int)sizeof(trace) - 1) {
        trace[trace_len++] = c;
        trace[trace_len] = '\0';
    }
}

static void reset_trace(void) {
    trace_len = 0;
    trace[0] = '\0';
}

static int trace_is(const char *want) {
    int i = 0;
    while (want[i] && trace[i] == want[i]) {
        i++;
    }
    return want[i] == trace[i];
}

static void h_a(void) { note('a'); }
static void h_b(void) { note('b'); }
static void h_c(void) { note('c'); }
static void h_q(void) { note('q'); }
static void h_late(void) { note('L'); }
static void h_registers(void) {
    note('r');
    atexit(h_late);  // registered while exiting: runs next
}
static void h_noop(void) { note('.'); }

static void test_atexit_order(void) {
    reset_trace();
    CHECK(atexit(h_a) == 0 && atexit(h_b) == 0 && atexit(h_c) == 0, "atexit succeeds");
    EXPECT_EXIT(exit(3), 3, "exit(3) with handlers");
    CHECK(trace_is("cba"), "handlers ran last first: \"%s\"", trace);

    reset_trace();
    EXPECT_EXIT(exit(0), 0, "exit(0) after the handlers ran");
    CHECK(trace_is(""), "each handler runs once: \"%s\"", trace);
}

static void test_handler_registering_handler(void) {
    reset_trace();
    atexit(h_a);
    atexit(h_registers);
    EXPECT_EXIT(exit(1), 1, "exit with a handler that registers another");
    CHECK(trace_is("rLa"), "the late handler runs before the earlier ones: \"%s\"", trace);
}

static void test_handler_limit(void) {
    reset_trace();
    int ok = 1;
    for (int i = 0; i < 32; i++) {
        ok &= atexit(h_noop) == 0;
    }
    CHECK(ok, "32 handlers fit (the standard minimum)");
    CHECK(atexit(h_noop) != 0, "the 33rd is refused");
    EXPECT_EXIT(exit(0), 0, "exit runs all 32");
    CHECK(trace_len == 32, "32 handlers ran (%d)", trace_len);
}

static void test_quick_exit(void) {
    reset_trace();
    atexit(h_a);
    CHECK(at_quick_exit(h_q) == 0 && at_quick_exit(h_b) == 0, "at_quick_exit succeeds");
    EXPECT_EXIT(quick_exit(5), 5, "quick_exit(5)");
    CHECK(trace_is("bq"), "quick_exit runs only its own handlers: \"%s\"", trace);

    reset_trace();
    EXPECT_EXIT(_Exit(2), 2, "_Exit with a pending atexit handler");
    EXPECT_EXIT(abort(), -6, "abort with a pending atexit handler");
    CHECK(trace_is(""), "_Exit and abort run no handlers: \"%s\"", trace);
    EXPECT_EXIT(exit(0), 0, "exit runs the pending handler");
    CHECK(trace_is("a"), "the pending handler ran: \"%s\"", trace);
}

TEST_MAIN(test_exit, test_underscore_exit, test_abort, test_atexit_order,
          test_handler_registering_handler, test_handler_limit, test_quick_exit)
