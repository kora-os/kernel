// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: what a screen manager takes from the kernel (console step 4b1):
// a signal at any process's end, the input queue and the screen.
//   inputprobe            runs every case below, one "inputprobe: <case>" line
//                         each, and "inputprobe: all passed" (exit 0) or a
//                         FAIL line (exit 1)
//   inputprobe keys N     claims the input queue and prints the next N key
//                         events ("inputprobe: key down 0x10 mods 0x0008 char
//                         0x6d"), for the smoke test's injected keys; gives up
//                         after 20 seconds (exit 1)
// The children it runs of itself:
//   inputprobe sleeper MS     sleeps MS ms and exits with 3
//   inputprobe waiter         sleeps until it is killed
//   inputprobe parent         starts a waiter, exits with its pid
//   inputprobe claimer WHAT   exits 0 if the input queue ("input") or the
//                             screen ("screen") is someone else's (its claim
//                             fails)
//   inputprobe holder         claims both and exits without giving them back
#include "../../user/libk/koraos.h"

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("inputprobe: FAIL %s\n", what);
        failures++;
    }
}

static int child(char *mode, char *arg) {
    char *argv[] = {"inputprobe", mode, arg};
    return spawn_flags("inputprobe", arg != NULL ? 3 : 2, argv, SPAWN_NOWAIT);
}

static int reap(int pid) {
    int code = -100;
    return waitpid(pid, &code, 0) == pid ? code : -100;
}

static int pending(int bit) {
    return (sig_set(0, 0) & SIG_MASK(bit)) != 0;
}

// --- The children --------------------------------------------------------------

static int sleeper(const char *ms) {
    msleep((unsigned)atoi(ms));
    return 3;
}

static int waiter(void) {
    int bit = sig_alloc(-1);
    sig_wait(SIG_MASK(bit));
    return 1;
}

static int parent(void) {
    int pid = child("waiter", NULL);
    msleep(10);  // let it start before we end
    return pid;
}

static int claimer(const char *what) {
    if (!strcmp(what, "input")) {
        int bit = sig_alloc(-1);
        struct input_event events[1];
        return input_claim(bit) == -1 && input_read(events, 1) == -1 && input_focus(1) == -1
                   ? 0 : 1;
    }
    return fb_claim(1) == -1 && fb_claim(0) == 0 ? 0 : 1;
}

static int holder(void) {
    int bit = sig_alloc(-1);
    struct fb_info fb;
    int ok = input_claim(bit) == 0;
    if (fb_info(&fb) == 0) {
        ok = ok && fb_claim(1) == 0;
    }
    return ok ? 0 : 1;  // and ends holding them
}

// --- The cases -----------------------------------------------------------------

static void end_signals(void) {
    int bit = sig_alloc(-1), unused = sig_alloc(-1);
    sig_free(unused);

    int pid = child("sleeper", "50");
    check(pid > 0, "spawn a sleeper");
    check(sig_on_end(pid, unused) == -1, "a watch needs an allocated bit");
    check(sig_on_end(1 << 30, bit) == -1, "and a process that exists");
    check(sig_on_end(pid, bit) == 0 && sig_on_end(pid, bit) == 0, "watch it (twice is once)");
    check(sig_wait(SIG_MASK(bit)) == SIG_MASK(bit), "its end sets the signal");
    check(reap(pid) == 3, "the sleeper exited with 3");
    check(!pending(bit), "a watch fires once");
    check(sig_on_end(pid, bit) == -1, "an ended process cannot be watched");

    pid = child("sleeper", "30");
    check(sig_on_end(pid, bit) == 0 && sig_on_end(pid, -1) == 0, "watch, then stop");
    check(reap(pid) == 3 && !pending(bit), "a stopped watch sets nothing");

    // Not a child: the waiter's parent ends first, the waiter is an orphan.
    int middle = child("parent", NULL);
    int orphan = reap(middle);
    check(orphan > 0 && sig_on_end(orphan, bit) == 0, "watch another program's child");
    check(kill(orphan) == 0 && sig_wait(SIG_MASK(bit)) == SIG_MASK(bit),
          "its end, killed, sets the signal");

    int other = sig_alloc(-1);
    pid = child("sleeper", "30");
    check(sig_on_end(pid, other) == 0 && sig_free(other) == 0, "watch on a bit, then free it");
    other = sig_alloc(other);
    check(reap(pid) == 3 && !pending(other), "a freed bit's watches set nothing");
    sig_free(other);
    sig_free(bit);
    printf("inputprobe: a signal at any process's end\n");
}

static void input_queue(void) {
    int bit = sig_alloc(-1), unused = sig_alloc(-1);
    sig_free(unused);
    struct input_event events[INPUT_READ_MAX + 1];
    check(input_read(events, 1) == -1 && input_focus(getpid()) == -1, "no claim, no events");
    check(input_claim(unused) == -1, "a claim needs an allocated bit");
    check(input_claim(bit) == 0, "claim the input queue");
    check(input_read(events, 0) == -1 && input_read(events, INPUT_READ_MAX + 1) == -1,
          "read 1 to 32 events");
    int count = input_read(events, INPUT_READ_MAX);
    check(count >= 0, "the claimer reads (keys typed before the claim are its)");
    check(input_focus(getpid()) == 0, "the claimer names the focus");
    check(reap(child("claimer", "input")) == 0, "another program's claim fails");
    check(input_claim(-1) == 0 && input_read(events, 1) == -1, "give it back");

    int other = sig_alloc(-1);
    check(input_claim(other) == 0 && sig_free(other) == 0 && input_read(events, 1) == -1,
          "freeing the claim's bit gives it back");
    check(reap(child("holder", NULL)) == 0, "a child claims and ends holding it");
    check(input_claim(bit) == 0, "the kernel took it back at its end");
    input_claim(-1);
    sig_free(bit);
    printf("inputprobe: the input queue, claimed and given back\n");
}

static void screen(void) {
    struct fb_info fb;
    if (fb_info(&fb) != 0 || fb.bpp != 32) {
        check(fb_claim(1) == -1, "no screen to claim");
        printf("inputprobe: no screen\n");
        return;
    }
    check(fb_claim(1) == 0 && fb_claim(1) == 0, "claim the screen (twice is once)");
    // Paint it red: the console must paint over it when it comes back.
    unsigned *pixels = (unsigned *)(unsigned long)fb.addr;
    for (unsigned y = 0; y < fb.height; y++) {
        unsigned *row = (unsigned *)((char *)pixels + (unsigned long)y * fb.pitch);
        for (unsigned x = 0; x < fb.width; x++) {
            row[x] = 0x00FF0000;
        }
    }
    printf("inputprobe: printed while the screen is claimed\n");
    check(reap(child("claimer", "screen")) == 0, "another program cannot claim it");
    check(fb_claim(0) == 0, "give it back");
    // Before anything is printed (output that scrolls redraws the screen
    // anyway): the console has painted over all of it.
    unsigned red = 0;
    for (unsigned y = 0; y < fb.height; y++) {
        unsigned *row = (unsigned *)((char *)pixels + (unsigned long)y * fb.pitch);
        for (unsigned x = 0; x < fb.width; x++) {
            red += (row[x] & 0x00FFFFFF) == 0x00FF0000;
        }
    }
    check(red == 0, "the console redraws the screen it gets back");
    check(reap(child("holder", NULL)) == 0, "a child claims it and ends holding it");
    check(fb_claim(1) == 0 && fb_claim(0) == 0, "the kernel took it back at its end");
    printf("inputprobe: the screen, claimed and given back\n");
}

// --- Keys ----------------------------------------------------------------------

static int timeout_bit;

static long timeout_main(void *arg) {
    msleep(20000);
    sig_send((int)(long)arg, SIG_MASK(timeout_bit));
    return 0;
}

static int keys(int wanted) {
    int key_bit = sig_alloc(-1);
    timeout_bit = sig_alloc(-1);
    if (input_claim(key_bit) != 0 || input_focus(getpid()) != 0) {
        printf("inputprobe: FAIL claim the input queue\n");
        return 1;
    }
    thread_create(timeout_main, (void *)(long)getpid(), -1);
    printf("inputprobe: ready for %d keys\n", wanted);
    fflush(stdout);
    int seen = 0;
    while (seen < wanted) {
        unsigned got = sig_wait(SIG_MASK(key_bit) | SIG_MASK(timeout_bit));
        struct input_event events[INPUT_READ_MAX];
        int count;
        while (seen < wanted && (count = input_read(events, INPUT_READ_MAX)) > 0) {
            for (int i = 0; i < count; i++, seen++) {
                printf("inputprobe: key %s 0x%02x mods 0x%04x char 0x%x\n",
                       events[i].type == INPUT_EVENT_KEY_DOWN ? "down" : "up",
                       events[i].keycode, events[i].modifiers, events[i].codepoint);
            }
        }
        if (got & SIG_MASK(timeout_bit)) {
            printf("inputprobe: FAIL %d of %d keys\n", seen, wanted);
            return 1;
        }
    }
    input_claim(-1);
    printf("inputprobe: keys done\n");
    return 0;  // the timeout thread dies with us
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "keys") && argc > 2) return keys(atoi(argv[2]));
        if (!strcmp(argv[1], "sleeper") && argc > 2) return sleeper(argv[2]);
        if (!strcmp(argv[1], "waiter")) return waiter();
        if (!strcmp(argv[1], "parent")) return parent();
        if (!strcmp(argv[1], "claimer") && argc > 2) return claimer(argv[2]);
        if (!strcmp(argv[1], "holder")) return holder();
        return 2;
    }
    end_signals();
    input_queue();
    screen();
    if (failures == 0) {
        printf("inputprobe: all passed\n");
    }
    return failures != 0;
}
