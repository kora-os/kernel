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
// The break signal: with signals on, a foreground child's end is a signal
// (bit 20), and the first wait sees Ctrl+C instead.
static bool signals_on, child_killed, break_allocated;
static int kills, breaks_sent;
static bool main_gone;  // the child's main thread ended, its others run
static unsigned waits, cleared;
#define END_BIT 20

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
// The standard handles the last child got (console unless spawn_std named
// others), and how many children got any.
static struct std_handle last_std[3];
static unsigned std_spawns;

// A child prints its name on its stdout, as a real one would: the console, or
// the file it was given.
static int kernel_spawn(const char *name, int argc, char *const argv[], int flags) {
    spawn_calls++;
    last_argc = argc;
    last_spawn_flags = flags;
    last_argument_terminated = argc > 0 && strcmp(argv[argc - 1], "a") == 0;
    if ((flags & 0xf00) == SPAWN_CORE(9) || strcmp(name, "nosuch") == 0) {
        return -1;  // no such core, or no such program
    }
    int out = last_std[1].kind == STD_FILE ? last_std[1].value : 1;
    write(out, "<", 1);
    write(out, name, strlen(name));
    write(out, ">\n", 2);
    if ((flags & SPAWN_NOWAIT) && (flags & (0x3f << 12)) == 0) {  // no end signal
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
    if (signals_on) {
        CHECK(pid == 123 && flags == WNOHANG, "a foreground child is reaped at its end signal");
        if (!child_killed || waits < 2) {
            return 0;  // it ends once killed, and its end is signalled
        }
        *code = -9;
        return pid;
    }
    CHECK(pid == 123 && flags == 0, "foreground wait matches child");
    *code = 0;
    return pid;
}
static long kernel(long number, long a0, long a1, long a2, long a3) {
    switch (number) {
    case SYS_SPAWN_FLAGS:
        memset(last_std, 0, sizeof last_std);
        return kernel_spawn((const char *)a0, (int)a1, (char *const *)a2, (int)a3);
    case SYS_SPAWN_STD: {
        const struct spawn_request *r = (const struct spawn_request *)a0;
        memcpy(last_std, r->std, sizeof last_std);
        std_spawns++;
        return kernel_spawn(r->name, r->argc, r->argv, r->flags);
    }
    case SYS_STD_HANDLES:
        memset((void *)a0, 0, 3 * sizeof(struct std_handle));  // the console
        return 0;
    case SYS_WAITPID:
        return kernel_waitpid((int)a0, (int *)a1, (int)a2);
    case SYS_GETCWD:
        return kernel_getcwd((char *)a0, (size_t)a1);
    case SYS_ASSIGN:
        return kernel_assign((const char *)a0, (const char *)a1);
    case SYS_CHDIR:
        return strncmp((const char *)a0, "nowhere", 7) == 0 ? -1 : 0;
    case SYS_VOLUME_INFO: {
        static const struct volume_info volumes[] = {
            {"sd0", "KORA", VOLUME_BOOT},
            {"usb0", "", VOLUME_READ_ONLY},
        };
        if (a0 > 1) return 0;
        *(struct volume_info *)a1 = volumes[a0];
        return 1;
    }
    case SYS_ASSIGN_INFO: {
        struct assign_info *info = (struct assign_info *)a1;
        if (a0 > 0) return 0;
        strcpy(info->name, "sys");
        strcpy(info->target, "boot:/");
        info->flags = ASSIGN_IMMUTABLE;
        return 1;
    }
    case SYS_KILL:
        child_killed |= a0 == 123;
        kills++;
        return a0 == 999 ? -1 : 0;
    case SYS_SIG_ALLOC:
        if (a0 == SIG_BREAK) {  // the shell handles breaks itself
            break_allocated = true;
            return SIG_BREAK;
        }
        return signals_on ? END_BIT : -1;
    case SYS_SIG_SEND:
        // A break for the child, which does not handle breaks: the kernel ends
        // it. With main_gone its main thread has ended: no thread to take it.
        if (a0 == 123 && a1 == SIG_MASK(SIG_BREAK)) {
            breaks_sent++;
            if (main_gone) {
                return -1;
            }
            child_killed = true;
        }
        return 0;
    case SYS_SIG_SET:
        cleared |= (unsigned)a1 & ~(unsigned)a0;
        return 0;
    case SYS_SIG_WAIT:
        CHECK(a0 == (SIG_MASK(END_BIT) | SIG_MASK(SIG_BREAK)), "it waits for the end or Ctrl+C");
        if (waits >= 10) {  // a break that kills nothing: end the test's child anyway
            CHECK(false, "the shell passes Ctrl+C on to its foreground program");
            child_killed = true;
        }
        return waits++ == 0 ? SIG_MASK(SIG_BREAK) : SIG_MASK(END_BIT);
    default:
        return -1;
    }
}

static void reset(void) {
    fflush(stdout);
    clearerr(stdin);  // an earlier test's end of input
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    input_size = read_chunk = 0;
    spawn_calls = assign_calls = background_reaps = std_spawns = 0;
    memset(last_std, 0, sizeof last_std);
    last_argc = last_spawn_flags = 0;
    background_job_live = false;
    last_argument_terminated = maximal_assign_received = false;
    signals_on = child_killed = break_allocated = false;
    waits = cleared = 0;
    kills = breaks_sent = 0;
    main_gone = false;
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
// Diagnostics go to stderr, and never to stdout as well.
static bool error_contains(const char *needle) {
    return strstr(fake_output(2, NULL), needle) != NULL && !output_contains(needle);
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
    CHECK(read_command_line() == -1 && spawn_calls == 0,
          "EOF before newline cannot dispatch prefix, and ends the input");
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
    CHECK(read_command_line() == 0 && error_contains("shell: line too long"),
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
    CHECK(session() == 0 && spawn_calls == 1 && last_argc == 2,
          "oversized command never dispatches its prefix and following command runs once");
    CHECK(error_contains("shell: line too long"), "dispatch path reports drained line");
}
static void argument_boundaries(void) {
    reset();
    append("echo");
    for (unsigned i = 0; i < 16; i++) append(" a");
    append("\necho a\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1 && last_argc == 2,
          "seventeenth token rejected without dispatch, next command runs");
    CHECK(error_contains("shell: too many arguments"), "excess arguments diagnostic");
    reset();
    append("echo");
    for (unsigned i = 0; i < 15; i++) append(" a");
    append(" \t\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1 && last_argc == 16,
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
    CHECK(session() == 0 && assign_calls == 1 && maximal_assign_received,
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
    CHECK(session() == 0 && spawn_calls == 1 && error_contains("shell: invalid quoting"),
          "malformed quoting never dispatches its prefix and next command runs");
}
static void background_jobs(void) {
    reset();
    append("echo a &\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1 && last_argc == 2 &&
          last_spawn_flags == SPAWN_NOWAIT && last_argument_terminated,
          "separate & starts a background job without passing \"&\" as an argument");
    CHECK(output_contains("[124] started") && output_contains("[124] done, exit 7") &&
          background_reaps == 1, "background job announced and reaped at the next prompt");
    reset();
    append("echo a&\necho a\nexit\n");
    CHECK(session() == 0 && spawn_calls == 2 && last_spawn_flags == 0 &&
          last_argument_terminated, "trailing & is stripped; the next command runs in front");
    CHECK(output_contains("[123] exited with 0"), "foreground job still waited for");
    reset();
    append("&\nexit\n");
    CHECK(session() == 0 && spawn_calls == 0, "a lone & runs nothing");
    reset();
    append("echo '&'\necho a\\&\nexit\n");
    CHECK(session() == 0 && spawn_calls == 2 && last_spawn_flags == 0 &&
          !background_reaps && !output_contains("started"),
          "quoted and escaped ampersands are arguments, not background jobs");
}

static void core_pinning(void) {
    reset();
    append("echo a @2\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1 && last_argc == 2 &&
          last_spawn_flags == SPAWN_CORE(2) && last_argument_terminated,
          "trailing @2 pins to core 2 and is not an argument");
    reset();
    append("echo a @3 &\nexit\n");
    CHECK(session() == 0 && last_argc == 2 &&
          last_spawn_flags == (SPAWN_NOWAIT | SPAWN_CORE(3)), "@N combines with &");
    reset();
    append("echo a '@2'\necho a \\@2\nexit\n");
    CHECK(session() == 0 && spawn_calls == 2 && last_argc == 3 && last_spawn_flags == 0,
          "quoted and escaped @2 are arguments");
    reset();
    append("echo a @x\necho a @\necho a @12345\nexit\n");
    CHECK(session() == 0 && spawn_calls == 0 &&
          error_contains("shell: @N needs a core number"), "malformed @N rejected, nothing run");
    reset();
    append("echo a @9\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1 &&
          error_contains("shell: cannot run echo on cpu 9"), "spawn failure on a pinned core reported");
}

static bool file_is(const char *path, const char *text) {
    size_t len = 0;
    const char *data = fake_file_data(path, &len);
    return data != NULL && len == strlen(text) && memcmp(data, text, len) == 0;
}

static void redirection(void) {
    reset();
    append("echo a > out.txt\nexit\n");
    CHECK(session() == 0 && std_spawns == 1 && last_argc == 2 && last_argument_terminated,
          "> takes the file out of argv");
    CHECK(last_std[0].kind == STD_CONSOLE && last_std[1].kind == STD_FILE && last_std[2].kind == STD_CONSOLE,
          "only stdout is the file");
    CHECK(file_is("out.txt", "<echo>\n") && !output_contains("<echo>"), "the child's output lands in it");
    CHECK(fake_open_fds() == 0, "the shell closes its copy");
    CHECK(output_contains("[123] exited with 0"), "and reports the job as usual");

    reset();
    fake_put_file("out.txt", "old\n", 4);
    append("echo a >out.txt\necho a >> out.txt\necho a >>out.txt\nexit\n");
    CHECK(session() == 0 && std_spawns == 3, "glued and separate operators");
    CHECK(file_is("out.txt", "<echo>\n<echo>\n<echo>\n"), "> truncates, >> appends");

    reset();
    fake_put_file("in.txt", "data\n", 5);
    append("cat < in.txt > out.txt &\nexit\n");
    CHECK(session() == 0 && std_spawns == 1 && last_argc == 1 &&
          last_spawn_flags == SPAWN_NOWAIT, "input and output together, in the background");
    CHECK(last_std[0].kind == STD_FILE && last_std[1].kind == STD_FILE &&
          last_std[0].value != last_std[1].value && fake_open_fds() == 0,
          "two files, both closed by the shell");

    reset();
    append("echo a > out.txt @2\nexit\n");
    CHECK(session() == 0 && std_spawns == 1 && last_argc == 2 &&
          last_spawn_flags == SPAWN_CORE(2), "redirection combines with @N");

    reset();
    append("echo '>' a\necho \\> a\nexit\n");
    CHECK(session() == 0 && spawn_calls == 2 && std_spawns == 0 && last_argc == 3,
          "quoted and escaped > are arguments");

    reset();
    fake_mkdir("dir");
    append("echo a < missing\necho a >\necho a > x > y\necho a < x < y\necho a > dir\nexit\n");
    CHECK(session() == 0 && spawn_calls == 0, "nothing runs with a bad redirection");
    CHECK(error_contains("shell: cannot open missing") && error_contains("shell: > needs a file") &&
          error_contains("shell: more than one output redirection") &&
          error_contains("shell: more than one input redirection") &&
          error_contains("shell: dir is a directory"), "each mistake is reported");
    CHECK(fake_open_fds() == 0, "and leaves no file open");
}

// What the shell writes for a short session: its prompts, the child's own
// output, and its lines, in that order. (The console echoes what is typed:
// on the screen each command follows its prompt.)
static void session_transcript(void) {
    reset();
    append("echo a\necho a &\nexit\n");
    CHECK(session() == 0, "a short session");
    fflush(stdout);
    CHECK(strcmp(fake_screen(NULL), "\nKoraOS shell. Type 'help'.\n"
                                    "$ <echo>\n"
                                    "[123] exited with 0\n"
                                    "$ <echo>\n"
                                    "[124] started\n"
                                    "[124] done, exit 7\n"
                                    "$ bye\n") == 0,
          "prompt, child output and job lines in order:\n%s", fake_screen(NULL));
    CHECK(strcmp(fake_output(2, NULL), "") == 0, "nothing on stderr");
}

static void builtins(void) {
    reset();
    append("pwd\ncd\ncd there\nother:\nvolumes\nassign\nsync\nkill 124\nexit\n");
    CHECK(session() == 0 && spawn_calls == 0, "built-ins run no program");
    fflush(stdout);
    CHECK(strcmp(fake_screen(NULL), "\nKoraOS shell. Type 'help'.\n"
                                    "$ boot:\n"           // pwd
                                    "$ boot:\n"           // cd
                                    "$ "                  // cd there
                                    "$ "                  // other:
                                    "$ sd0: KORA [boot]\n" // volumes
                                    "usb0: (unlabelled) [read-only]\n"
                                    "$ sys: = boot:/ [fixed]\n"  // assign
                                    "$ "                  // sync
                                    "$ "                  // kill 124
                                    "$ bye\n") == 0,
          "pwd, cd, volumes and assign print their listings:\n%s", fake_screen(NULL));
    CHECK(strcmp(fake_output(2, NULL), "") == 0 && fake_syncs() == 1, "quietly, and sync syncs");

    reset();
    append("help\nexit\n");
    CHECK(session() == 0 && output_contains("$ KoraOS shell commands:\n  help ") &&
              output_contains("  (try 'ls /bin')\n$ bye\n"),
          "help prints the command list");
}

static void builtin_errors(void) {
    reset();
    append("pwd x\ncd a b\ncd nowhere\nnowhere:\nvolumes x\nsync x\nassign a b c\n"
           "kill\nkill 12x\nkill 999\nnosuch\nexit\n");
    fake_fail_sync(1);
    CHECK(session() == 0, "bad built-in commands");
    CHECK(strcmp(fake_output(2, NULL), "usage: pwd\n"
                                       "usage: cd [path]\n"
                                       "cd: cannot change directory to nowhere\n"
                                       "cd: cannot change directory to nowhere:\n"
                                       "usage: volumes\n"
                                       "usage: sync\n"
                                       "usage: assign [name [target]]\n"
                                       "usage: kill <pid>\n"
                                       "usage: kill <pid>\n"
                                       "kill: no such program: 999\n"
                                       "shell: no such program: nosuch\n") == 0,
          "each gets its usage or its reason, on stderr");
    CHECK(!output_contains("usage") && !output_contains("cannot") && fake_syncs() == 0,
          "and nothing of it on stdout");
    reset();
    append("sync\nexit\n");
    fake_fail_sync(1);
    CHECK(session() == 0 && error_contains("sync: failed\n"), "a failed sync is reported");
}

static void end_of_input(void) {
    reset();
    append("echo a\n");
    CHECK(session() == 0 && spawn_calls == 1, "the end of input ends the shell");
    CHECK(output_contains("[123] exited with 0\n$ \nbye\n"), "after the last command");
}

static void break_ends_the_foreground(void) {
    reset();
    signals_on = true;
    append("echo a\nexit\n");
    CHECK(session() == 0 && spawn_calls == 1, "a foreground program");
    CHECK(last_spawn_flags == (SPAWN_NOWAIT | SPAWN_SIGNAL(END_BIT)),
          "starts at once, its end a signal (flags %#x)", last_spawn_flags);
    CHECK(cleared & SIG_MASK(SIG_BREAK), "an earlier Ctrl+C is forgotten first");
    CHECK(break_allocated, "the shell handles breaks itself");
    CHECK(breaks_sent == 1 && kills == 0 && child_killed && waits == 2 &&
              output_contains("[123] exited with -9"),
          "Ctrl+C is passed on to it as a break, which ends it, and its end is reported");
    reset();
    signals_on = main_gone = true;
    append("echo a\nexit\n");
    CHECK(session() == 0 && breaks_sent == 1 && kills == 1 && child_killed &&
              output_contains("[123] exited with -9"),
          "a program whose main thread has ended is killed instead");
    reset();
    signals_on = true;
    append("echo a &\nexit\n");
    CHECK(session() == 0 && last_spawn_flags == SPAWN_NOWAIT && waits == 0 && !child_killed,
          "a background job is not waited for");
}

static void host_needs_a_manager(void) {
    reset();
    CHECK(shell_test_main() == 1 && error_contains("shell: no screen manager runs"),
          "on the kernel's console with no screen manager, the shell says so");
}

TEST_MAIN(fragmented_reads, line_boundaries, drained_line_not_dispatched,
          argument_boundaries, maximal_assign_line, quoted_tokens, background_jobs, core_pinning,
          redirection, session_transcript, builtins, builtin_errors, end_of_input,
          break_ends_the_foreground, host_needs_a_manager)
