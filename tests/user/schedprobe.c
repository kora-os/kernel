// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for the scheduler.
//   schedprobe sleep <ms>  msleep(ms), then report how long it really took
//                          (exit 1 if it returned early).
//   schedprobe spin <ms>   busy-loop for ms of wall time without a single
//                          syscall: the rest of the system (the shell) only
//                          keeps running meanwhile if the timer preempts us
//                          (or it runs on another core). Reports the longest
//                          gap the loop saw: time slices lost to others.
//   schedprobe spin <ms> <after>  the same after sleeping <after> ms: several
//                          spinners started one by one then measure the
//                          same stretch, after the commands that started them
//                          were typed and shown (the shell and its console
//                          run on cores of their own, a spinner's too).
#include "../../user/libk/koraos.h"

static unsigned long parse_ms(const char *s) {
    unsigned long v = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (unsigned long)(*s - '0');
    }
    return v;
}

int main(int argc, char **argv) {
    if (argc != 3 && !(argc == 4 && strcmp(argv[1], "spin") == 0)) {
        fputs("usage: schedprobe sleep <ms> | spin <ms> [after]\n", stderr);
        return 2;
    }
    unsigned long ms = parse_ms(argv[2]);
    if (argc == 4) {
        msleep(parse_ms(argv[3]));
    }
    unsigned long start = uptime_us();
    if (strcmp(argv[1], "sleep") == 0) {
        msleep(ms);
        unsigned long took = (uptime_us() - start) / 1000;
        printf("schedprobe: slept %lu ms\n", took);
        return took >= ms ? 0 : 1;
    }
    if (strcmp(argv[1], "spin") == 0) {
        // Also report the longest stretch the loop lost the CPU for: whole
        // time slices if another task shares this core, next to nothing if
        // it has the core to itself.
        unsigned long last = start;
        unsigned long longest = 0;
        while (last - start < ms * 1000) {
            unsigned long now = uptime_us();
            if (now - last > longest) {
                longest = now - last;
            }
            last = now;
        }
        printf("schedprobe: spun %lu ms, longest gap %lu us\n", (uptime_us() - start) / 1000,
               longest);
        return 0;
    }
    fputs("usage: schedprobe sleep <ms> | spin <ms> [after]\n", stderr);
    return 2;
}
