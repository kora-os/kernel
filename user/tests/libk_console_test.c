// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for libk's console rules (user/libk/koraos.h) on the fake
// kernel: the flush of stdout before the console is handed over to a child
// started with spawn(). stdout is line buffered, so text without a newline
// would otherwise come out after what the child wrote.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"
#include "libk/koraos.h"

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(NULL);
}

static int console_is(const char *want) {
    return strcmp(fake_output(1, NULL), want) == 0;
}

// The "kernel" side of spawn: the child writes straight to the console, then
// the call returns its pid.
static int spawned_flags;

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a1;
    (void)a2;
    if (number == SYS_SPAWN_FLAGS) {
        const char *name = (const char *)a0;
        spawned_flags = (int)a3;
        write(1, name, strlen(name));
        write(1, " ran\n", 5);
        return 42;
    }
    return -1;
}

static void test_output_before_child(void) {
    reset();
    fake_set_handler(kernel);
    printf("starting: ");
    CHECK(spawn("child", 0, NULL) == 42 && spawned_flags == 0, "spawn reaches the kernel");
    CHECK(console_is("starting: child ran\n"), "the parent's buffered text comes out first");
    reset();
    fake_set_handler(kernel);
    printf("in the background: ");
    CHECK(spawn_flags("job", 0, NULL, SPAWN_NOWAIT | SPAWN_CORE(2)) == 42 &&
              spawned_flags == (SPAWN_NOWAIT | SPAWN_CORE(2)),
          "spawn_flags passes its flags on");
    CHECK(console_is("in the background: job ran\n"), "and flushes too");
}

TEST_MAIN(test_output_before_child)
