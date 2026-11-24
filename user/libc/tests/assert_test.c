// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for <assert.h> and src/assert/assert.c: a true assertion does
// nothing, a false one writes its report to stderr in one write and aborts
// (exit code -6, through the fake kernel's SYS_EXIT trap), the expression is
// evaluated once, NDEBUG removes the assertion (and its side effects) at each
// inclusion of the header, and static_assert is _Static_assert.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#include <assert.h>

static fake_jmp_buf env;
static int evaluated;

static int count(int value) {
    evaluated++;
    return value;
}

static_assert(sizeof(int) >= 2, "static_assert is available in C11");

static void test_true(void) {
    evaluated = 0;
    assert(count(1));
    assert(1 + 1 == 2);
    CHECK(evaluated == 1, "a true assertion evaluates its expression once");
    CHECK(fake_writes(2) == 0, "and writes nothing");
}

static int report_line;

static void fails(void) {
    report_line = __LINE__ + 1;
    assert(count(2) == 3 && "two is three");
}

static void test_false(void) {
    fake_reset_console();
    evaluated = 0;
    if (fake_setjmp(&env) == 0) {
        fake_arm_exit(&env);
        fails();
        CHECK(0, "assert returned");
    }
    CHECK(fake_exit_status() == -6, "a false assertion aborts: status %d", fake_exit_status());
    CHECK(evaluated == 1, "after evaluating its expression once");
    char want[256];
    snprintf(want, sizeof want,
             "Assertion failed: count(2) == 3 && \"two is three\", function fails, file %s, "
             "line %d\n",
             __FILE__, report_line);
    const char *got = fake_output(2, NULL);
    CHECK(strcmp(got, want) == 0, "the report: \"%s\"", got);
    CHECK(fake_writes(2) == 1, "in one write: %u", fake_writes(2));
    CHECK(fake_output(1, NULL)[0] == '\0', "nothing on stdout");
}

static void long_name_fails(void) {
    assert(0 && "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}

static void test_long_message(void) {
    fake_reset_console();
    if (fake_setjmp(&env) == 0) {
        fake_arm_exit(&env);
        long_name_fails();
    }
    size_t len = 0;
    const char *got = fake_output(2, &len);
    CHECK(fake_exit_status() == -6 && len == 511 && got[len - 1] == '\n' &&
              strncmp(got, "Assertion failed: 0 && \"aaa", 27) == 0,
          "a long report is cut short and still ends its line: %zu bytes", len);
}

// The header again, with NDEBUG: assert() now does nothing at all.
#define NDEBUG
#include <assert.h>

static void test_ndebug(void) {
    evaluated = 0;
    assert(count(0));
    CHECK(evaluated == 0, "with NDEBUG the expression is not evaluated");
}

// And without it again.
#undef NDEBUG
#include <assert.h>

static void test_again(void) {
    fake_reset_console();
    if (fake_setjmp(&env) == 0) {
        fake_arm_exit(&env);
        assert(count(0));
    }
    CHECK(fake_exit_status() == -6 && strncmp(fake_output(2, NULL), "Assertion failed: count(0)", 26) == 0,
          "including the header again without NDEBUG brings assert back");
    fake_reset_console();
}

TEST_MAIN(test_true, test_false, test_long_message, test_ndebug, test_again)
