// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for src/signal/signal.c and src/stdlib/abort.c: handlers are
// installed and returned, raise() calls them in the caller, the default
// action ends the program with the negative signal number, and abort() ends
// it whatever a returning handler does. The fake kernel turns SYS_EXIT into
// a jump back to the test.

#include "test.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include "fake_syscall.h"

static fake_jmp_buf env;

#define EXPECT_EXIT(call, want, what)                                           \
    do {                                                                        \
        if (fake_setjmp(&env) == 0) {                                           \
            fake_arm_exit(&env);                                                \
            call;                                                               \
            CHECK(0, "%s returned", what);                                      \
        } else {                                                                \
            CHECK(fake_exit_status() == (want), "%s: status %d, want %d", what, \
                  fake_exit_status(), (want));                                  \
        }                                                                       \
    } while (0)

static const int all_signals[] = {SIGINT, SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTERM};
#define SIGNALS (sizeof all_signals / sizeof all_signals[0])

static volatile sig_atomic_t last_signal;
static int calls;

static void record(int sig) {
    last_signal = sig;
    calls++;
}

static void other(int sig) {
    last_signal = -sig;
    calls++;
}

static void reset(void) {
    for (size_t i = 0; i < SIGNALS; i++) {
        signal(all_signals[i], SIG_DFL);
    }
    last_signal = 0;
    calls = 0;
}

static void test_numbers(void) {
    CHECK(SIGINT == 2 && SIGILL == 4 && SIGABRT == 6 && SIGFPE == 8 && SIGSEGV == 11 &&
              SIGTERM == 15,
          "the usual signal numbers");
    CHECK(SIG_DFL != SIG_IGN && SIG_DFL != SIG_ERR && SIG_IGN != SIG_ERR,
          "SIG_DFL, SIG_IGN and SIG_ERR are distinct");
    CHECK(SIG_DFL != record && SIG_IGN != record && SIG_ERR != record,
          "and distinct from a function");
}

static void test_install(void) {
    reset();
    for (size_t i = 0; i < SIGNALS; i++) {
        int sig = all_signals[i];
        CHECK(signal(sig, record) == SIG_DFL, "signal %d starts as SIG_DFL", sig);
        CHECK(signal(sig, other) == record, "signal %d returns the previous handler", sig);
        CHECK(signal(sig, SIG_IGN) == other, "signal %d: second handler returned", sig);
        CHECK(signal(sig, SIG_DFL) == SIG_IGN, "signal %d: SIG_IGN returned", sig);
    }
}

static void test_invalid(void) {
    reset();
    static const int bad[] = {0, -1, 1, 3, 5, 7, 9, 10, 12, 14, 16, 31, 64, 100000};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        errno = 0;
        CHECK(signal(bad[i], record) == SIG_ERR && errno == EINVAL,
              "signal(%d) fails with EINVAL", bad[i]);
        errno = 0;
        CHECK(raise(bad[i]) != 0 && errno == EINVAL, "raise(%d) fails with EINVAL", bad[i]);
    }
    errno = 0;
    CHECK(signal(SIGINT, SIG_ERR) == SIG_ERR && errno == EINVAL, "SIG_ERR is not a handler");
    CHECK(signal(SIGINT, record) == SIG_DFL, "a failed signal() installs nothing");
    CHECK(calls == 0, "no handler ran");
}

static void test_raise(void) {
    reset();
    for (size_t i = 0; i < SIGNALS; i++) {
        int sig = all_signals[i];
        signal(sig, record);
        int before = calls;
        CHECK(raise(sig) == 0, "raise(%d) succeeds", sig);
        CHECK(calls == before + 1 && last_signal == sig, "the handler got %d", sig);
        // The handler stays installed.
        CHECK(raise(sig) == 0 && calls == before + 2, "signal %d is delivered again", sig);
        CHECK(signal(sig, record) == record, "signal %d keeps its handler", sig);
    }
    // One signal's handler is not another's.
    reset();
    signal(SIGINT, record);
    signal(SIGTERM, other);
    raise(SIGTERM);
    CHECK(last_signal == -SIGTERM && calls == 1, "each signal has its own handler");
}

static void test_ignore(void) {
    reset();
    for (size_t i = 0; i < SIGNALS; i++) {
        signal(all_signals[i], SIG_IGN);
        CHECK(raise(all_signals[i]) == 0, "an ignored signal %d: raise returns 0", all_signals[i]);
    }
    CHECK(calls == 0, "ignored signals call nothing");
}

static void test_default_action(void) {
    reset();
    EXPECT_EXIT(raise(SIGINT), -2, "raise(SIGINT)");
    EXPECT_EXIT(raise(SIGILL), -4, "raise(SIGILL)");
    EXPECT_EXIT(raise(SIGABRT), -6, "raise(SIGABRT)");
    EXPECT_EXIT(raise(SIGFPE), -8, "raise(SIGFPE)");
    EXPECT_EXIT(raise(SIGSEGV), -11, "raise(SIGSEGV)");
    EXPECT_EXIT(raise(SIGTERM), -15, "raise(SIGTERM)");
    // Back to the default after a handler.
    signal(SIGTERM, record);
    signal(SIGTERM, SIG_DFL);
    EXPECT_EXIT(raise(SIGTERM), -15, "raise(SIGTERM) after SIG_DFL");
    CHECK(calls == 0, "the removed handler did not run");
}

static int depth;

static void nested(int sig) {
    calls++;
    if (++depth < 3) {
        raise(sig);  // not blocked while its handler runs
    }
}

static void replaces_itself(int sig) {
    calls++;
    signal(sig, record);
}

static void test_handler_calls(void) {
    reset();
    depth = 0;
    signal(SIGINT, nested);
    CHECK(raise(SIGINT) == 0 && calls == 3, "a handler may raise its own signal: %d calls", calls);
    reset();
    signal(SIGINT, replaces_itself);
    raise(SIGINT);
    raise(SIGINT);
    CHECK(calls == 2 && last_signal == SIGINT, "a handler may install another");
}

static fake_jmp_buf escape;

static void jumps_out(int sig) {
    last_signal = sig;
    fake_longjmp(&escape, 1);
}

static void test_abort(void) {
    reset();
    EXPECT_EXIT(abort(), -6, "abort()");
    // A handler that returns does not save the program.
    signal(SIGABRT, record);
    EXPECT_EXIT(abort(), -6, "abort() with a returning handler");
    CHECK(calls == 1 && last_signal == SIGABRT, "abort() raised SIGABRT first");
    // Nor does ignoring the signal.
    signal(SIGABRT, SIG_IGN);
    EXPECT_EXIT(abort(), -6, "abort() with SIGABRT ignored");
    // A handler that does not return does.
    signal(SIGABRT, jumps_out);
    last_signal = 0;
    if (fake_setjmp(&escape) == 0) {
        abort();
        CHECK(0, "abort returned");
    }
    CHECK(last_signal == SIGABRT, "a handler that jumps out survives abort()");
}

TEST_MAIN(test_numbers, test_install, test_invalid, test_raise, test_ignore, test_default_action,
          test_handler_calls, test_abort)
