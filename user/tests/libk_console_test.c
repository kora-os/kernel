// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for libk's console code (user/libk/koraos.h) on the fake
// kernel: kgetline's line editing.

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

TEST_MAIN(test_line_editing)
