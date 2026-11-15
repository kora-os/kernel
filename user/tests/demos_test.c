// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests of the demos on the fake kernel: gfxdemo, termdemo, jobdemo and
// pindemo. The test stands in for what the fake kernel does not have: a
// framebuffer (host memory), a clock that moves a fixed step per reading,
// threads that run to completion when created, and children that are only
// recorded. So every line the demos print is fixed and checked exactly.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main gfxdemo_main
#include "../gfxdemo.c"
#undef main
#define main termdemo_main
#include "../termdemo.c"
#undef main
#define main jobdemo_main
#include "../jobdemo.c"
#undef main
#define main pindemo_main
#include "../pindemo.c"
#undef main

// The clock: koraos.h leaves uptime_us() to the host test.
static unsigned long clock_us, clock_step;

unsigned long uptime_us(void) {
    clock_us += clock_step;
    return clock_us;
}

// Threads start in libk's assembly stub on the target; here the kernel side
// below runs them itself.
void libk_thread_start(void) {
}

#define FB_WIDTH 16
#define FB_HEIGHT 8
#define FB_STRIDE 20  // wider than the picture: the padding must stay untouched
static unsigned framebuffer[FB_HEIGHT * FB_STRIDE];
static int have_framebuffer;
static unsigned cpus;

#define MAX_THREADS 32
static long thread_results[MAX_THREADS];
static int thread_cores[MAX_THREADS];
static unsigned threads, thread_limit;
static unsigned long thread_cost_us;

#define MAX_CHILDREN 8
static struct {
    char rounds[24];
    int flags;
} children[MAX_CHILDREN];
static unsigned spawned, reaped, kills;
static int spawn_fails_at;             // the child (0, 1, ...) that cannot start, or -1
static int reap_order[MAX_CHILDREN];   // which child each waitpid returns
static int reap_codes[MAX_CHILDREN];   // and its exit code

static long kernel(long number, long a0, long a1, long a2, long a3) {
    switch (number) {
    case SYS_FB_INFO: {
        if (!have_framebuffer) {
            return -1;
        }
        struct fb_info *fb = (struct fb_info *)a0;
        fb->addr = (unsigned long)framebuffer;
        fb->width = FB_WIDTH;
        fb->height = FB_HEIGHT;
        fb->pitch = FB_STRIDE * 4;
        fb->bpp = 32;
        return 0;
    }
    case SYS_SYSINFO:
        memset((void *)a0, 0, sizeof(struct sysinfo));
        ((struct sysinfo *)a0)->cpus = cpus;
        return 0;
    case SYS_THREAD_NEW:
        if (threads == thread_limit) {
            return -1;
        }
        thread_cores[threads] = (int)a3;
        thread_results[threads] = ((thread_fn)a1)((void *)a2);
        clock_us += thread_cost_us;
        return 100 + (long)threads++;
    case SYS_THREAD_JOIN:
        *(long *)a1 = thread_results[a0 - 100];
        return 0;
    case SYS_SPAWN_FLAGS: {
        char *const *argv = (char *const *)a2;
        if ((int)spawned == spawn_fails_at) {
            return -1;
        }
        CHECK(strcmp((const char *)a0, "pindemo") == 0 && a1 == 3 && strcmp(argv[1], "-w") == 0,
              "a worker is pindemo -w <rounds>");
        strcpy(children[spawned].rounds, argv[2]);
        children[spawned].flags = (int)a3;
        return 50 + (long)spawned++;
    }
    case SYS_WAITPID:
        if (a0 == -1) {
            *(int *)a1 = reap_codes[reaped];
            return 50 + reap_order[reaped++];
        }
        return a0;
    case SYS_KILL:
        kills++;
        return 0;
    case SYS_GETPID:
        return 7;
    case SYS_TASK_INFO: {
        // Two tasks; this program's main thread is the second, on core 2.
        struct task_info *info = (struct task_info *)a1;
        if (a0 > 1) {
            return 0;
        }
        memset(info, 0, sizeof(*info));
        info->pid = info->tid = a0 == 0 ? 1 : 7;
        info->core = a0 == 0 ? 0 : 2;
        return 1;
    }
    default:
        return -1;
    }
}

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    memset(framebuffer, 0x55, sizeof(framebuffer));
    have_framebuffer = 0;
    cpus = 4;
    clock_us = 0;
    clock_step = 1000;
    threads = 0;
    thread_limit = MAX_THREADS;
    thread_cost_us = 0;
    spawned = reaped = kills = 0;
    spawn_fails_at = -1;
}

// main returns here instead of going through exit(), so stdout is flushed by
// hand.
#define RUN(program, ...) start(program##_main, (char *[]){__VA_ARGS__, NULL})

static int start(int (*program)(int, char **), char **argv) {
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }
    int status = program(argc, argv);
    fflush(stdout);
    return status;
}

static int printed(const char *text, const char *errors) {
    return strcmp(fake_output(1, NULL), text) == 0 && strcmp(fake_output(2, NULL), errors) == 0;
}

static unsigned pixel(unsigned x, unsigned y) {
    return framebuffer[y * FB_STRIDE + x];
}

static void test_gfxdemo(void) {
    reset();
    CHECK(RUN(gfxdemo, "gfxdemo") == 1 && printed("", "gfxdemo: no framebuffer\n"),
          "gfxdemo without a framebuffer");
    reset();
    have_framebuffer = 1;
    CHECK(RUN(gfxdemo, "gfxdemo") == 0 && printed("gfxdemo: painted 16x8\n", ""),
          "gfxdemo reports the size it painted");
    int gradient = 1, padding = 1;
    for (unsigned y = 0; y < FB_HEIGHT; y++) {
        for (unsigned x = 0; x < FB_WIDTH; x++) {
            gradient &= pixel(x, y) == (((x * 255 / FB_WIDTH) << 16) | ((y * 255 / FB_HEIGHT) << 8));
        }
        for (unsigned x = FB_WIDTH; x < FB_STRIDE; x++) {
            padding &= pixel(x, y) == 0x55555555u;
        }
    }
    CHECK(gradient, "red rises with x, green with y");
    CHECK(padding, "the row padding past the width is left alone");
    reset();
    have_framebuffer = 1;
    CHECK(RUN(gfxdemo, "gfxdemo", "blue") == 0 && pixel(0, 0) == 0xffu &&
              pixel(FB_WIDTH - 1, FB_HEIGHT - 1) == 0xffu,
          "a colour name fills the screen");
    reset();
    have_framebuffer = 1;
    CHECK(RUN(gfxdemo, "gfxdemo", "mauve") == 0 && pixel(0, 0) == 0 &&
              pixel(FB_WIDTH - 1, 0) == (FB_WIDTH - 1) * 255 / FB_WIDTH << 16,
          "an unknown name paints the gradient");
}

static int termdemo_run(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return termdemo_main();
}
#define termdemo_main termdemo_run

static void test_termdemo(void) {
    reset();
    CHECK(RUN(termdemo, "termdemo") == 0 && strcmp(fake_output(2, NULL), "") == 0, "termdemo");
    size_t len;
    const unsigned char *bytes = (const unsigned char *)fake_output(1, &len);
    unsigned long long hash = 0xcbf29ce484222325ull;  // FNV-1a
    for (size_t i = 0; i < len; i++) {
        hash = (hash ^ bytes[i]) * 0x100000001b3ull;
    }
    // The stream of the original write()-per-piece version, which the screen
    // terminal is known to render (the QEMU smoke test takes a screenshot).
    CHECK(len == 4805 && hash == 0xd19acecfd99c2b70ull,
          "the escape sequence stream is byte for byte the original one (%zu bytes, hash %llx)",
          len, hash);
    static const char opening[] = "\x1b[2J\x1b[H\x1b[1;4mKoraOS terminal demo\x1b[0m\n";
    CHECK(memcmp(bytes, opening, sizeof(opening) - 1) == 0, "it starts by clearing the screen");
    CHECK(strstr((const char *)bytes, "\x1b[48;5;231m ") != NULL &&
              strstr((const char *)bytes, "\x1b[48;2;252;3;128m ") != NULL &&
              strstr((const char *)bytes, "\x1b" "7\x1b[1;100H") != NULL,
          "256-colour, 24-bit colour and cursor addressing sequences");
    CHECK(fake_writes(1) < 40, "in a few dozen writes, not one per piece (%u)", fake_writes(1));
}

static void test_jobdemo(void) {
    reset();
    thread_cost_us = 3000;
    CHECK(RUN(jobdemo, "jobdemo", "16") == 0, "jobdemo without a framebuffer");
    // 1 job: 1 ms of clock + 3 ms of thread; 4 jobs one after the other: 13 ms.
    CHECK(printed("jobdemo: 640x480, 16 iterations: 1 core 4 ms, 4 cores 13 ms, speedup 0.30x\n",
                  ""),
          "the result line, with two decimals of speedup");
    CHECK(threads == 5 && thread_cores[0] == 0 && thread_cores[1] == 0 && thread_cores[2] == 1 &&
              thread_cores[3] == 2 && thread_cores[4] == 3,
          "one job on core 0, then one per core");
    CHECK(thread_results[0] > 0 &&
              thread_results[0] == thread_results[1] + thread_results[2] + thread_results[3] +
                                       thread_results[4],
          "the bands add up to the whole picture");
    CHECK(fake_writes(1) == 1, "the line goes out in one write");

    reset();
    cpus = 1;
    CHECK(RUN(jobdemo, "jobdemo", "16") == 0 &&
              printed("jobdemo: 640x480, 16 iterations: 1 core 1 ms, 1 core 1 ms, speedup 1.00x\n",
                      ""),
          "one core is not \"1 cores\"");

    reset();
    have_framebuffer = 1;
    cpus = 2;
    fake_set_input("\r", 1, 0);
    CHECK(RUN(jobdemo, "jobdemo", "64") == 0, "jobdemo on a framebuffer");
    CHECK(printed("jobdemo: 16x8, 64 iterations: 1 core 1 ms, 2 cores 1 ms, speedup 1.00x, "
                  "press Enter\n\n",
                  ""),
          "it asks for Enter, and the key is echoed");
    CHECK(fake_input_position() == 1, "and waits for it");
    int painted = 0, padding = 1;
    for (unsigned y = 0; y < FB_HEIGHT; y++) {
        for (unsigned x = 0; x < FB_STRIDE; x++) {
            painted |= x < FB_WIDTH && pixel(x, y) != 0x55555555u && pixel(x, y) != 0;
            padding &= x < FB_WIDTH || pixel(x, y) == 0x55555555u;
        }
    }
    CHECK(painted && padding, "the picture is on the framebuffer, inside its width");

    reset();
    thread_limit = 2;
    CHECK(RUN(jobdemo, "jobdemo", "16") == 1 && printed("", "jobdemo: FAILED to queue the jobs\n"),
          "a job that cannot start fails the demo");
    reset();
    CHECK(RUN(jobdemo, "jobdemo", "15") == 2 && RUN(jobdemo, "jobdemo", "4097") == 2 &&
              RUN(jobdemo, "jobdemo", "1x") == 2 && RUN(jobdemo, "jobdemo", "16", "16") == 2 &&
              threads == 0,
          "bad arguments start nothing");
    CHECK(printed("", "usage: jobdemo [iterations]   (16 to 4096, default 1024)\n"
                      "usage: jobdemo [iterations]   (16 to 4096, default 1024)\n"
                      "usage: jobdemo [iterations]   (16 to 4096, default 1024)\n"
                      "usage: jobdemo [iterations]   (16 to 4096, default 1024)\n"),
          "and print the usage on stderr");
}

static void reap(unsigned n, const int order[], const int codes[]) {
    for (unsigned i = 0; i < n; i++) {
        reap_order[i] = order[i];
        reap_codes[i] = codes[i];
    }
}

static void test_pindemo(void) {
    // Calibration reads the clock twice around 65536 rounds: 60 ms apart
    // here, so a 100 ms unit is 65536 * 100 / 60 = 109226 rounds.
    reset();
    clock_step = 60000;
    reap(2, (int[]){1, 0}, (int[]){3, 1});
    CHECK(RUN(pindemo, "pindemo", "2@1", "1") == 0, "pindemo with two workers");
    CHECK(spawned == 2 && strcmp(children[0].rounds, "218452") == 0 &&
              strcmp(children[1].rounds, "109226") == 0,
          "each worker gets its units as rounds");
    CHECK(children[0].flags == (SPAWN_NOWAIT | SPAWN_CORE(1)) && children[1].flags == SPAWN_NOWAIT,
          "in the background, pinned only when asked");
    CHECK(printed("pindemo: worker 2 (1 unit, cpu 3) done after 60 ms\n"
                  "pindemo: worker 1 (2 units, cpu 1 pinned) done after 120 ms\n"
                  "pindemo: 2 workers done in 180 ms\n",
                  ""),
          "one line per worker as it finishes, then the total");
    CHECK(fake_writes(1) == 3, "each line in one write");

    reset();
    clock_step = 60000;
    reap(2, (int[]){0, 1}, (int[]){2, EXIT_KILLED});
    CHECK(RUN(pindemo, "pindemo", "2@1", "1") == 1, "a worker on the wrong core fails the demo");
    CHECK(printed("pindemo: worker 1 (2 units, cpu 2 pinned) done after 60 ms\n"
                  "pindemo: worker 2 (1 unit, cpu ?) done after 120 ms\n",
                  "pindemo: FAILED, a worker ran on the wrong core\n"),
          "a killed worker has no core to report");

    reset();
    clock_step = 60000;
    spawn_fails_at = 1;
    CHECK(RUN(pindemo, "pindemo", "1", "1@9", "1") == 1 && spawned == 1 && kills == 1,
          "a worker that cannot start ends the ones already running");
    CHECK(printed("", "pindemo: cannot start worker 2 (no such core?)\n"), "and says which");

    reset();
    clock_step = 60000;
    reap(4, (int[]){0, 1, 2, 3}, (int[]){1, 2, 2, 2});
    CHECK(RUN(pindemo, "pindemo") == 0 && spawned == 4 &&
              children[0].flags == (SPAWN_NOWAIT | SPAWN_CORE(1)) &&
              children[3].flags == (SPAWN_NOWAIT | SPAWN_CORE(2)) &&
              strcmp(children[0].rounds, "655356") == 0,
          "the default on four cores: 6@1 2@2 2@2 2@2");
    reset();
    clock_step = 60000;
    cpus = 2;
    reap(4, (int[]){0, 1, 2, 3}, (int[]){0, 1, 0, 1});
    CHECK(RUN(pindemo, "pindemo") == 0 && spawned == 4 && children[0].flags == SPAWN_NOWAIT &&
              strcmp(children[0].rounds, "327678") == 0,
          "the default on two cores: 3 1 1 1, unpinned");

    reset();
    CHECK(RUN(pindemo, "pindemo", "0") == 2 && RUN(pindemo, "pindemo", "101") == 2 &&
              RUN(pindemo, "pindemo", "3@x") == 2 && RUN(pindemo, "pindemo", "x") == 2 &&
              spawned == 0,
          "bad specs are refused before anything starts");
    CHECK(printed("", "usage: pindemo [units[@core] ...]   (1 to 100 units of ~100 ms)\n"
                      "usage: pindemo [units[@core] ...]   (1 to 100 units of ~100 ms)\n"
                      "usage: pindemo [units[@core] ...]   (1 to 100 units of ~100 ms)\n"
                      "usage: pindemo [units[@core] ...]   (1 to 100 units of ~100 ms)\n"),
          "with the usage on stderr");
    reset();
    CHECK(RUN(pindemo, "pindemo", "1", "1", "1", "1", "1", "1", "1", "1", "1") == 2 &&
              printed("", "pindemo: at most 8 workers\n") && spawned == 0,
          "more than eight workers are refused");

    reset();
    CHECK(RUN(pindemo, "pindemo", "-w", "1000") == 2 && printed("", ""),
          "a worker prints nothing and exits with the core it ran on");
}

TEST_MAIN(test_gfxdemo, test_termdemo, test_jobdemo, test_pindemo)
