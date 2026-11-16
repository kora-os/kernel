// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for src/setjmp/setjmp.S. The code is AArch64 assembly, so this
// test only builds on arm64 hosts (see CMakeLists.txt); on the target the
// same checks run in libcprobe under the QEMU smoke tests. Built without the
// sanitizers: ASan has to be told about every stack frame a longjmp
// abandons, which it does for the host's longjmp only.

#include "test.h"

#include <setjmp.h>

long setjmp_registers(jmp_buf env);

static jmp_buf env;
static int depth_reached;

static void test_direct_return(void) {
    CHECK(setjmp(env) == 0, "setjmp returns 0 when called");
}

static void test_value(void) {
    volatile int passes = 0;
    int value = setjmp(env);
    passes = passes + 1;
    if (value == 0) {
        longjmp(env, 42);
    }
    CHECK(value == 42 && passes == 2, "longjmp value 42: got %d after %d passes", value,
          (int)passes);
}

static void test_zero_becomes_one(void) {
    int value = setjmp(env);
    if (value == 0) {
        longjmp(env, 0);
    }
    CHECK(value == 1, "longjmp(env, 0) makes setjmp return 1: got %d", value);
    volatile int again = 0;
    value = setjmp(env);
    if (again == 0) {
        again = 1;
        longjmp(env, -3);
    }
    CHECK(value == -3, "a negative value passes through: got %d", value);
}

__attribute__((noinline)) static void dive(int depth) {
    volatile char pad[200];  // makes each frame a real one
    pad[0] = (char)depth;
    if (depth == 0) {
        depth_reached = 1;
        longjmp(env, 7);
    }
    dive(depth - 1);
    pad[1] = pad[0];  // keeps the call from becoming a jump
}

static void test_through_frames(void) {
    volatile long before = 0x1234567890abcdefl;
    volatile double fbefore = 2.5;
    depth_reached = 0;
    int value = setjmp(env);
    if (value == 0) {
        dive(40);
        CHECK(0, "dive returned");
    }
    CHECK(value == 7 && depth_reached == 1, "longjmp out of 40 frames");
    CHECK(before == 0x1234567890abcdefl && fbefore == 2.5, "locals survive the jump");
}

static void test_two_buffers(void) {
    static jmp_buf outer, inner;
    volatile int step = 0;
    if (setjmp(outer) == 0) {
        if (setjmp(inner) == 0) {
            step = 1;
            longjmp(inner, 1);
        }
        step = step + 10;
        longjmp(outer, 1);
    }
    CHECK(step == 11, "nested buffers are independent: step %d", (int)step);
}

static void test_registers(void) {
    static jmp_buf regs;
    long mask = setjmp_registers(regs);
    CHECK(mask == 0, "callee-saved registers restored: mask 0x%lx", mask);
}

TEST_MAIN(test_direct_return, test_value, test_zero_becomes_one, test_through_frames,
          test_two_buffers, test_registers)
