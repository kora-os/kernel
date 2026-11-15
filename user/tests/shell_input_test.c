// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of the actual shell (user/shell.c) on the fake kernel: the real
// parser and main loop read scripted console input and write to the fake
// console; the calls the fake kernel does not serve (spawn, waitpid, cwd,
// volumes, assigns) come to this file's handler.

#include "test.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main shell_test_main
#include "../shell.c"
#undef main

static char input[3 * LINE_MAX];
static size_t input_size, read_chunk;
static unsigned spawn_calls, assign_calls, background_reaps;
static int last_argc, last_spawn_flags;
static bool background_job_live;
static bool last_argument_terminated, maximal_assign_received;

static int kernel_getcwd(char *buffer, size_t size) {
    if (size < 6) return -1;
    memcpy(buffer, "boot:", 6);
    return 0;
}
static int kernel_assign(const char *name, const char *target) {
    assign_calls++;
    maximal_assign_received = name && target && strlen(name) == 31 &&
                              strlen(target) == KORA_PATH_MAX - 1;
    return 0;
}
// A child prints straight to the console, as a real one does.
static int kernel_spawn(const char *name, int argc, char *const argv[], int flags) {
    spawn_calls++;
    last_argc = argc;
    last_spawn_flags = flags;
    last_argument_terminated = argc > 0 && strcmp(argv[argc - 1], "a") == 0;
    if ((flags & 0xf00) == SPAWN_CORE(9)) {
        return -1;  // no such core
    }
    write(1, "<", 1);
    write(1, name, strlen(name));
    write(1, ">\n", 2);
    if (flags & SPAWN_NOWAIT) {
        background_job_live = true;
        return 124;
    }
    return 123;
}
// A background job (pid 124) has finished by the next prompt.
static int kernel_waitpid(int pid, int *code, int flags) {
    if (pid == -1) {
        CHECK(flags == WNOHANG, "job reaping never blocks the prompt");
        if (!background_job_live) return 0;
        background_job_live = false;
        background_reaps++;
        *code = 7;
        return 124;
    }
    CHECK(pid == 123 && flags == 0, "foreground wait matches child");
    *code = 0;
    return pid;
}
static long kernel(long number, long a0, long a1, long a2, long a3) {
    switch (number) {
    case SYS_SPAWN_FLAGS:
        return kernel_spawn((const char *)a0, (int)a1, (char *const *)a2, (int)a3);
    case SYS_WAITPID:
        return kernel_waitpid((int)a0, (int *)a1, (int)a2);
    case SYS_GETCWD:
        return kernel_getcwd((char *)a0, (size_t)a1);
    case SYS_ASSIGN:
        return kernel_assign((const char *)a0, (const char *)a1);
    case SYS_CHDIR:
    case SYS_VOLUME_INFO:  // no volumes
    case SYS_ASSIGN_INFO:  // no assigns
        return 0;
    default:
        return -1;
    }
}

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    input_size = read_chunk = 0;
    spawn_calls = assign_calls = background_reaps = 0;
    last_argc = last_spawn_flags = 0;
    background_job_live = false;
    last_argument_terminated = maximal_assign_received = false;
}
// The console input so far; the next read starts at its first byte again.
static void feed(void) {
    fake_set_input(input, input_size, read_chunk);
}
static void append(const char *text) {
    while (*text) input[input_size++] = *text++;
    feed();
}
static void fill(char byte, size_t count) {
    while (count-- > 0) input[input_size++] = byte;
    feed();
}
static void set_read_chunk(size_t chunk) {
    read_chunk = chunk;
    feed();
}
static bool output_contains(const char *needle) {
    fflush(stdout);
    return strstr(fake_output(1, NULL), needle) != NULL;
}
static void fragmented_reads(void) {
    reset();
    append("echo a\nexit\n");
    set_read_chunk(2);
    CHECK(read_command_line() == 1 && strcmp(line, "echo a\n") == 0,
          "split console reads assembled before dispatch");
    CHECK(fake_input_position() == 7, "next command remains unread");
    CHECK(read_command_line() == 1 && strcmp(line, "exit\n") == 0, "next fragmented command intact");
    reset();
    append("echo a");
    CHECK(read_command_line() == 0 && spawn_calls == 0, "EOF before newline cannot dispatch prefix");
}
static void line_boundaries(void) {
    reset();
    fill('a', LINE_MAX - 2);
    append("\n");
    CHECK(read_command_line() == 1 && fake_input_position() == LINE_MAX - 1 &&
          line[LINE_MAX - 2] == '\n' && line[LINE_MAX - 1] == 0,
          "largest complete line accepted and terminated");
    reset();
    fill('a', LINE_MAX + 133);
    append("\n");
    append("echo a\nexit\n");
    CHECK(read_command_line() == 0 && output_contains("shell: line too long"),
          "oversized line drained and rejected");
    CHECK(read_command_line() == 1 && strcmp(line, "echo a\n") == 0,
          "draining leaves subsequent command intact");
    reset();
    fill('a', LINE_MAX - 1);
    append("\n");
    append("exit\n");
    CHECK(read_command_line() == 0 && read_command_line() == 1 && strcmp(line, "exit\n") == 0,
          "full buffer without newline rejected at exact boundary");
}
static void drained_line_not_dispatched(void) {
    reset();
    append("echo a ");
    while (input_size < LINE_MAX + 133) append(" ");
    append("\necho a\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && last_argc == 2,
          "oversized command never dispatches its prefix and following command runs once");
    CHECK(output_contains("shell: line too long"), "dispatch path reports drained line");
}
static void argument_boundaries(void) {
    reset();
    append("echo");
    for (unsigned i = 0; i < 16; i++) append(" a");
    append("\necho a\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && last_argc == 2,
          "seventeenth token rejected without dispatch, next command runs");
    CHECK(output_contains("shell: too many arguments"), "excess arguments diagnostic");
    reset();
    append("echo");
    for (unsigned i = 0; i < 15; i++) append(" a");
    append(" \t\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && last_argc == 16,
          "sixteen tokens accepted with trailing whitespace");
    CHECK(last_argument_terminated, "last accepted token excludes trailing whitespace/newline");
}
static void maximal_assign_line(void) {
    reset();
    append("assign ");
    fill('n', 31);
    append(" ");
    fill('t', KORA_PATH_MAX - 1);
    append("\nexit\n");
    set_read_chunk(97);
    CHECK(shell_test_main() == 0 && assign_calls == 1 && maximal_assign_received,
          "maximum assign name and qualified target reach syscall intact");
    CHECK(spawn_calls == 0, "assign built-in never dispatches as an ELF command");
}
static void quoted_tokens(void) {
    char *argv[MAX_ARGV];
    char text[] = "cp 'single quoted.txt' \"double\\\" quoted.txt\" escaped\\ name.txt '' ab\"cd\"\\ ef & '&'";
    int count = tokenize(text, argv);
    CHECK(count == 8, "quotes, escapes, empty argument and concatenation tokenized");
    if (count == 8) {
        CHECK(strcmp(argv[1], "single quoted.txt") == 0 && strcmp(argv[2], "double\" quoted.txt") == 0 &&
              strcmp(argv[3], "escaped name.txt") == 0 && strcmp(argv[4], "") == 0 &&
              strcmp(argv[5], "abcd ef") == 0, "quote decoding preserves argument contents");
        CHECK(strcmp(argv[6], "&") == 0 && !literal_tokens[6] &&
              strcmp(argv[7], "&") == 0 && literal_tokens[7], "quoted operators retain literal provenance");
    }
    char single[] = "echo 'back\\slash' résumé-notes.txt";
    CHECK(tokenize(single, argv) == 3 && strcmp(argv[1], "back\\slash") == 0 &&
          strcmp(argv[2], "résumé-notes.txt") == 0, "single quotes preserve slashes and lexer preserves UTF-8 bytes");
    const char *invalid[] = {"echo 'open", "echo \"open", "echo trailing\\", "echo \"escape\\\n"};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        char malformed[64];
        unsigned j = 0;
        do { malformed[j] = invalid[i][j]; } while (invalid[i][j++]);
        CHECK(tokenize(malformed, argv) == -2, "malformed quote/escape rejected");
    }
    reset();
    append("echo 'unterminated\necho a\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && output_contains("shell: invalid quoting"),
          "malformed quoting never dispatches its prefix and next command runs");
}
static void background_jobs(void) {
    reset();
    append("echo a &\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && last_argc == 2 &&
          last_spawn_flags == SPAWN_NOWAIT && last_argument_terminated,
          "separate & starts a background job without passing \"&\" as an argument");
    CHECK(output_contains("[124] started") && output_contains("[124] done, exit 7") &&
          background_reaps == 1, "background job announced and reaped at the next prompt");
    reset();
    append("echo a&\necho a\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 2 && last_spawn_flags == 0 &&
          last_argument_terminated, "trailing & is stripped; the next command runs in front");
    CHECK(output_contains("[123] exited with 0"), "foreground job still waited for");
    reset();
    append("&\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 0, "a lone & runs nothing");
    reset();
    append("echo '&'\necho a\\&\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 2 && last_spawn_flags == 0 &&
          !background_reaps && !output_contains("started"),
          "quoted and escaped ampersands are arguments, not background jobs");
}

static void core_pinning(void) {
    reset();
    append("echo a @2\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 && last_argc == 2 &&
          last_spawn_flags == SPAWN_CORE(2) && last_argument_terminated,
          "trailing @2 pins to core 2 and is not an argument");
    reset();
    append("echo a @3 &\nexit\n");
    CHECK(shell_test_main() == 0 && last_argc == 2 &&
          last_spawn_flags == (SPAWN_NOWAIT | SPAWN_CORE(3)), "@N combines with &");
    reset();
    append("echo a '@2'\necho a \\@2\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 2 && last_argc == 3 && last_spawn_flags == 0,
          "quoted and escaped @2 are arguments");
    reset();
    append("echo a @x\necho a @\necho a @12345\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 0 &&
          output_contains("shell: @N needs a core number"), "malformed @N rejected, nothing run");
    reset();
    append("echo a @9\nexit\n");
    CHECK(shell_test_main() == 0 && spawn_calls == 1 &&
          output_contains("shell: cannot run echo on cpu 9"), "spawn failure on a pinned core reported");
}

TEST_MAIN(fragmented_reads, line_boundaries, drained_line_not_dispatched,
          argument_boundaries, maximal_assign_line, quoted_tokens, background_jobs, core_pinning)
