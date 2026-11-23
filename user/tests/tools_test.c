// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests of the small tools on the fake kernel: echo, hello, cat, ls,
// mkdir, rmdir, rm, mv, ps and init. Each program's actual source is
// included with its main renamed; a test runs it with an argv and checks its
// exit status, what it printed on stdout and on stderr, and what it did to
// the filesystem. Messages belong on stderr, results on stdout.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main echo_main
#include "../echo.c"
#undef main
#define main hello_main
#include "../hello.c"
#undef main
#define main cat_main
#include "../cat.c"
#undef main
#define main ls_main
#include "../ls.c"
#undef main
#define main mkdir_main
#include "../mkdir.c"
#undef main
#define main rmdir_main
#include "../rmdir.c"
#undef main
#define main rm_main
#include "../rm.c"
#undef main
#define main mv_main
#include "../mv.c"
#undef main
#define main ps_main
#include "../ps.c"
#undef main
#define main init_main
#include "../init.c"
#undef main

// The calls the fake kernel leaves to the test: a directory listing, a task
// list, and a child that prints straight to the console.
static const struct listing {
    const char *name;
    int is_dir;
} *listing;
static unsigned listing_size, listing_next;
static int readdir_fails;
static int spawn_result, child_exit_code;

// The standard handles the kernel reports (have_handles 0: none, the kernel
// console), and a console handler on port 5 that hands out `typed` in reads,
// a line at a time, then the end of input.
#define HANDLER_PORT 5
static int have_handles;
static struct std_handle handles[3];
static const char *typed;
static int modes_asked;
static struct con_packet *pending;
static int pending_reply;

static const struct task_info tasks[] = {
    {.pid = 1, .ppid = 0, .core = 0, .state = TASK_SLEEPING, .name = "init", .tid = 1},
    {.pid = 12, .ppid = 2, .core = 3, .state = TASK_RUNNING, .name = "ps", .tid = 12},
    {.pid = 7, .ppid = 2, .core = 1, .state = TASK_READY, .name = "jobdemo", .tid = 105},
    {.pid = 9, .ppid = 2, .core = 2, .state = 99, .name = "odd", .tid = 9},
};

// The clock: koraos.h leaves uptime_us() to the host test. Each reading is
// `clock_step` later; init's shell runs `shell_runs` times before it stops.
static unsigned long clock_us, clock_step;
static int have_display, shell_runs;
static char spawned_as[64];

unsigned long uptime_us(void) {
    clock_us += clock_step;
    return clock_us;
}

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a2;
    (void)a3;
    switch (number) {
    case SYS_READDIR: {
        struct dirent *out = (struct dirent *)a1;
        if (readdir_fails) {
            return -1;
        }
        if (listing_next == listing_size) {
            return 0;
        }
        strcpy(out->name, listing[listing_next].name);
        out->is_dir = listing[listing_next].is_dir;
        out->size = 0;
        listing_next++;
        return 1;
    }
    case SYS_TASK_INFO:
        if ((unsigned long)a0 >= sizeof(tasks) / sizeof(tasks[0])) {
            return 0;
        }
        *(struct task_info *)a1 = tasks[a0];
        return 1;
    case SYS_SPAWN_FLAGS:
        spawned_as[0] = '\0';
        for (long i = 0; i < a1; i++) {
            strcat(spawned_as, i ? " " : "");
            strcat(spawned_as, ((char *const *)a2)[i]);
        }
        if (spawn_result > 0) {
            write(1, "<shell>\n", 8);
            if (--shell_runs == 0) {
                clock_step = 1000;  // the next run ends at once
            }
        }
        return spawn_result;
    case SYS_FB_INFO:
        if (!have_display) {
            return -1;
        }
        *(struct fb_info *)a0 = (struct fb_info){0x1000, 640, 480, 2560, 32};
        return 0;
    case SYS_WAITPID:
        *(int *)a1 = child_exit_code;
        return a0;
    case SYS_STD_HANDLES:
        if (!have_handles) {
            return -1;
        }
        memcpy((void *)a0, handles, sizeof(handles));
        return 0;
    case SYS_PORT_CREATE:
        return 100;
    case SYS_PORT_PUT: {
        if (a0 != HANDLER_PORT) {
            return -1;
        }
        struct con_packet *packet = (struct con_packet *)a1;
        if (packet->op == CON_MODE) {
            modes_asked++;
            packet->result = packet->length == CON_COOKED ? 0 : -1;
        } else if (packet->op == CON_READ) {
            size_t n = 0;
            while (n < packet->length && typed[n] && (n == 0 || typed[n - 1] != '\n')) {
                ((char *)packet->buf)[n] = typed[n];
                n++;
            }
            typed += n;
            packet->result = (long)n;
        } else {
            packet->result = -1;
        }
        pending = packet;
        pending_reply = (int)a2;
        return 0;
    }
    case SYS_PORT_WAIT: {
        struct port_message *out = (struct port_message *)a1;
        CHECK(pending != NULL && a0 == pending_reply, "a packet comes back to its reply port");
        *out = (struct port_message){pending, PORT_REPLY, 9};
        pending = NULL;
        return 1;
    }
    default:
        return -1;
    }
}

// Give the program stdin as `handle`, and stdout and stderr as the console,
// as if it had just started (the libc asks the kernel again).
static void stdin_is(struct std_handle handle) {
    fake_reset_std_streams();
    have_handles = 1;
    handles[0] = handle;
    handles[1] = handles[2] = (struct std_handle){STD_CONSOLE, 0};
}

// Back to the kernel console for all three.
static void console_stdin(void) {
    fake_reset_std_streams();
    have_handles = 0;
}

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    listing_size = listing_next = 0;
    readdir_fails = 0;
    have_display = shell_runs = 0;
    clock_step = 0;
}

// Runs a program's main with the given arguments (argv[0] first). main
// returns here instead of going through exit(), so stdout is flushed by hand.
#define RUN(program, ...) run(program##_run, (char *[]){__VA_ARGS__, NULL})

static int run(int (*program)(int, char **), char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }
    int status = program(argc, argv);
    fflush(stdout);
    return status;
}

#define echo_run echo_main
#define cat_run cat_main
#define ls_run ls_main
#define mkdir_run mkdir_main
#define rmdir_run rmdir_main
#define rm_run rm_main
#define mv_run mv_main

// The programs whose main takes no arguments.
static int hello_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return hello_main();
}

static int ps_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return ps_main();
}

static int init_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return init_main();
}

static int printed(const char *out, const char *err) {
    return strcmp(fake_output(1, NULL), out) == 0 && strcmp(fake_output(2, NULL), err) == 0;
}

static int screen_is(const char *want) {
    return strcmp(fake_screen(NULL), want) == 0;
}

static int is_dir(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && info.is_dir;
}

static int exists(const char *path) {
    struct stat info;
    return stat(path, &info) == 0;
}

static void put(const char *path, const char *text) {
    fake_put_file(path, text, strlen(text));
}

static void test_echo_hello(void) {
    reset();
    CHECK(RUN(echo, "echo", "a", "b c", "d") == 3 && printed("a b c d\n", ""),
          "echo joins its arguments and returns their count");
    reset();
    CHECK(RUN(echo, "echo") == 0 && printed("\n", ""), "echo alone prints a newline");
    reset();
    CHECK(RUN(hello, "hello") == 0 && printed("Hello from userland (ELF)\n", ""), "hello");
}

static void test_cat(void) {
    reset();
    put("one", "first\nsecond");
    put("two", "third\n");
    CHECK(RUN(cat, "cat", "one", "two") == 0 && printed("first\nsecondthird\n", ""),
          "cat prints its files one after the other");
    reset();
    put("one", "first\nsecond");
    put("two", "third\n");
    CHECK(RUN(cat, "cat", "one", "missing", "two") == 1, "a missing file fails cat");
    CHECK(printed("first\nsecondthird\n", "cat: cannot open missing\n"),
          "the others are still printed, the message goes to stderr");
    CHECK(screen_is("first\nsecondcat: cannot open missing\nthird\n"),
          "and comes after what was printed before it, even with no newline");
    CHECK(fake_open_fds() == 0, "every file is closed");

    reset();
    fake_mkdir("folder");
    CHECK(RUN(cat, "cat", "folder") == 1 && printed("", "cat: cannot read folder\n") &&
              fake_open_fds() == 0,
          "a directory opens but cannot be read");
    reset();
    CHECK(RUN(cat, "cat") == 1 &&
              printed("", "usage: cat [path...] (with no path, stdin: a file or a console window)\n"),
          "with no path on the kernel console, which has no end of input: usage");

    // With no path, stdin: a file, and a console window's lines to Ctrl+D.
    reset();
    put("in.txt", "from a file\nline two\n");
    int in = open("in.txt", O_RDONLY);
    stdin_is((struct std_handle){STD_FILE, in});
    CHECK(RUN(cat, "cat") == 0 && printed("from a file\nline two\n", ""), "cat < file");
    reset();
    typed = "typed\nlines\n";
    modes_asked = 0;
    stdin_is((struct std_handle){STD_PORT, HANDLER_PORT});
    CHECK(RUN(cat, "cat") == 0 && printed("typed\nlines\n", "") && modes_asked == 1,
          "cat in a console window: its lines, to the end of input, in cooked mode");
    console_stdin();

    // Binary data through short reads from the file and short console writes.
    reset();
    static char data[5000];
    for (unsigned i = 0; i < sizeof(data); i++) {
        data[i] = (char)(i * 7 + 1);
        if (data[i] == '\0') {
            data[i] = '\n';  // keep it a C string for the comparison
        }
    }
    data[sizeof(data) - 1] = '\0';
    put("blob", data);
    fake_set_file_chunks(7, 0);
    fake_set_write_chunk(5);
    CHECK(RUN(cat, "cat", "blob") == 0 && printed(data, ""), "short reads and writes lose nothing");
}

static void test_ls(void) {
    static const struct listing entries[] = {{"bin", 1}, {"readme.txt", 0}, {"résumé", 0}};
    reset();
    fake_mkdir(".");
    listing = entries;
    listing_size = 3;
    CHECK(RUN(ls, "ls") == 0 && printed("bin/\nreadme.txt\nrésumé\n", ""),
          "ls lists the current directory, directories with a slash");
    CHECK(fake_open_fds() == 0, "and closes it");
    reset();
    fake_mkdir("docs");
    CHECK(RUN(ls, "ls", "docs") == 0 && printed("", ""), "an empty directory prints nothing");
    reset();
    CHECK(RUN(ls, "ls", "missing") == 1 && printed("", "ls: cannot open missing\n"),
          "a missing directory");
    reset();
    fake_mkdir("docs");
    readdir_fails = 1;
    CHECK(RUN(ls, "ls", "docs") == 1 && fake_open_fds() == 0, "a failing readdir fails ls");
}

static void test_mkdir_rmdir(void) {
    reset();
    CHECK(RUN(mkdir, "mkdir", "a", "b") == 0 && is_dir("a") && is_dir("b") && printed("", ""),
          "mkdir creates each directory");
    CHECK(fake_syncs() == 1, "and syncs once");
    CHECK(RUN(mkdir, "mkdir", "a", "c") == 1 && is_dir("c") &&
              printed("", "mkdir: cannot create a\n"),
          "an existing name fails, the others are still created");
    CHECK(fake_syncs() == 2, "and what was created is synced");
    reset();
    CHECK(RUN(mkdir, "mkdir") == 1 && printed("", "usage: mkdir directory...\n") &&
              fake_syncs() == 0,
          "mkdir usage");

    reset();
    fake_mkdir("a");
    put("file", "x");
    CHECK(RUN(rmdir, "rmdir", "a") == 0 && !exists("a") && printed("", ""), "rmdir removes");
    CHECK(RUN(rmdir, "rmdir", "file", "missing") == 1 && exists("file") &&
              printed("", "rmdir: cannot remove file\nrmdir: cannot remove missing\n"),
          "rmdir refuses a file and a missing name");
    reset();
    CHECK(RUN(rmdir, "rmdir") == 1 && printed("", "usage: rmdir empty-directory...\n"),
          "rmdir usage");
}

static void test_rm_mv(void) {
    reset();
    put("a", "1");
    put("b", "2");
    fake_mkdir("folder");
    CHECK(RUN(rm, "rm", "a", "folder", "b") == 1 && !exists("a") && !exists("b") &&
              is_dir("folder") && printed("", "rm: cannot remove folder\n"),
          "rm removes files, never a directory, and carries on after a failure");
    CHECK(fake_syncs() == 1, "rm syncs once");
    reset();
    CHECK(RUN(rm, "rm") == 1 && printed("", "usage: rm file...\n"), "rm usage");

    reset();
    put("old", "contents");
    put("taken", "other");
    CHECK(RUN(mv, "mv", "old", "new") == 0 && !exists("old") && printed("", ""), "mv renames");
    size_t len;
    CHECK(fake_file_data("new", &len) != NULL && len == 8, "the contents move with the name");
    CHECK(RUN(mv, "mv", "new", "taken") == 1 && exists("new") &&
              printed("", "mv: cannot rename new\n"),
          "mv never replaces an existing file");
    reset();
    put("old", "contents");
    fake_fail_sync(1);
    CHECK(RUN(mv, "mv", "old", "new") == 1 && printed("", "mv: sync failed\n"),
          "a failed sync is not reported as success");
    reset();
    CHECK(RUN(mv, "mv", "one") == 1 && printed("", "usage: mv source new-destination\n") &&
              fake_syncs() == 0,
          "mv usage");
}

static void test_ps(void) {
    reset();
    CHECK(RUN(ps, "ps") == 0, "ps");
    CHECK(printed("  PID   TID  PPID  CPU  STATE     NAME\n"
                  "    1     1     0    0  sleeping  init\n"
                  "   12    12     2    3  running   ps\n"
                  "    7   105     2    1  ready     jobdemo\n"
                  "    9     9     2    2  ?         odd\n",
                  ""),
          "one aligned row per task, an unknown state as ?");
}

static void test_init(void) {
    reset();
    CHECK(RUN(init, "init") == 0 &&
              printed("init: no display, so no shell; the serial line is the debug console\n", ""),
          "without a display, no shell");
    reset();
    have_display = 1;
    spawn_result = 5;
    child_exit_code = 3;
    clock_step = 2000000;
    shell_runs = 3;
    CHECK(RUN(init, "init") == 3 && strcmp(spawned_as, "screens shell") == 0,
          "with one, the shell under the screen manager");
    CHECK(screen_is("init: starting the shell (screens shell)\n<shell>\n"
                    "init: the shell ended (exit 3); starting it again\n"
                    "init: starting the shell (screens shell)\n<shell>\n"
                    "init: the shell ended (exit 3); starting it again\n"
                    "init: starting the shell (screens shell)\n<shell>\n"
                    "init: the shell ended at once (exit 3); not starting it again\n"),
          "started again as it ends, until it ends at once:\n%s", fake_screen(NULL));
    reset();
    have_display = 1;
    spawn_result = -1;
    CHECK(RUN(init, "init") == 1 &&
              printed("init: starting the shell (screens shell)\n",
                      "init: cannot start /bin/screens\n"),
          "init reports a manager that cannot start");
}

TEST_MAIN(test_echo_hello, test_cat, test_ls, test_mkdir_rmdir, test_rm_mv, test_ps, test_init)
