// SPDX-License-Identifier: GPL-3.0-or-later
//
// EL0 regression fixture for a break a program handles itself
// (project_doc/libc-plan.md, step 21). It allocates SIG_BREAK, which says it
// handles breaks: Ctrl+C then only sets the bit instead of ending it.
//
// It checks the allocation rules first (SIG_BREAK is the one system bit a
// program may take, sig_alloc(-1) never picks it), then prints
// "breakprobe: waiting for a break" and waits for one. On the first Ctrl+C it
// prints "breakprobe: caught a break" and is stuck from then on: it sleeps
// and never clears the bit. The next Ctrl+C is left pending, and the one
// after it ends the program anyway (exit code -9).
//
// A failed check prints itself on stderr and the program exits with 1.
#include "../../user/libk/koraos.h"

static int failed;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "breakprobe: FAILED %s\n", what);
        failed = 1;
    }
}

int main(void) {
    check(sig_alloc(1) == -1, "the other system bits stay the system's");
    int any = sig_alloc(-1);
    check(any >= 8 && any != SIG_BREAK, "sig_alloc(-1) picks a bit of its own");
    check(sig_alloc(SIG_BREAK) == SIG_BREAK, "SIG_BREAK can be allocated");
    check(sig_alloc(SIG_BREAK) == -1, "once");
    check(sig_free(SIG_BREAK) == 0 && sig_alloc(SIG_BREAK) == SIG_BREAK,
          "and freed and allocated again");
    check((sig_set(0, 0) & SIG_MASK(SIG_BREAK)) == 0, "it starts clear");
    if (failed) {
        return 1;
    }

    puts("breakprobe: waiting for a break");
    fflush(stdout);
    unsigned got = sig_wait(SIG_MASK(SIG_BREAK));
    check(got == SIG_MASK(SIG_BREAK), "the break wakes sig_wait");
    if (failed) {
        return 1;
    }
    puts("breakprobe: caught a break");
    fflush(stdout);
    for (;;) {
        msleep(1000);  // stuck: the next break stays set, the one after ends it
    }
}
