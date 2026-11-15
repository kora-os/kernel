// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for libk's console rules (user/libk/koraos.h) on the fake
// kernel: kgetline's line editing, and the flush of stdout before the
// console is handed over, to kgetline's raw read and echo and to a child
// started with spawn(). stdout is line buffered, so text without a newline
// would otherwise come out after what the other side wrote.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"
#include "libk/koraos.h"

static void type(const char *keys) {
    fake_set_input(keys, strlen(keys), 0);
}

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(NULL);
}

static int console_is(const char *want) {
    return strcmp(fake_output(1, NULL), want) == 0;
}

static void test_line_editing(void) {
    char buffer[16];
    reset();
    type("ab\x7f" "c\x1b[A\x1b[1;5D\x1bOx\x01" "d\r" "next\n");
    ssize_t count = kgetline(buffer, sizeof(buffer) - 1);
    buffer[count > 0 ? count : 0] = 0;
    CHECK(count == 4 && strcmp(buffer, "acd\n") == 0,
          "backspace edits, CR ends the line, escapes and control bytes dropped");
    CHECK(console_is("ab\b \bcd\n"), "printable bytes, rubout and newline echoed");
    count = kgetline(buffer, sizeof(buffer) - 1);
    buffer[count > 0 ? count : 0] = 0;
    CHECK(count == 5 && strcmp(buffer, "next\n") == 0, "typed-ahead line left for the next call");
    reset();
    type("xyz\n");
    count = kgetline(buffer, 2);
    CHECK(count == 2 && buffer[0] == 'x' && buffer[1] == 'y' && fake_input_position() == 2,
          "a full buffer returns without reading further");
    reset();
    CHECK(kgetline(buffer, sizeof(buffer)) == 0, "EOF with nothing read passes read's 0 through");
}

static void test_prompt_before_input(void) {
    char buffer[16];
    reset();
    type("yes\r");
    printf("continue? ");
    CHECK(console_is(""), "a prompt without a newline waits in stdout's buffer");
    CHECK(kgetline(buffer, sizeof(buffer)) == 4, "kgetline reads the answer");
    CHECK(console_is("continue? yes\n"), "after flushing the prompt, so the echo follows it");
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

TEST_MAIN(test_line_editing, test_prompt_before_input, test_output_before_child)
