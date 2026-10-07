// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for the scheduler.
//   schedprobe sleep <ms>  msleep(ms), then report how long it really took
//                          (exit 1 if it returned early).
//   schedprobe spin <ms>   busy-loop for ms of wall time without a single
//                          syscall: the rest of the system (the shell) only
//                          keeps running meanwhile if the timer preempts us.
#include "../../user/libk/koraos.h"

static unsigned long parse_ms(const char *s) {
    unsigned long v = 0;
    for (; *s >= '0' && *s <= '9'; s++) {
        v = v * 10 + (unsigned long)(*s - '0');
    }
    return v;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        kputs("usage: schedprobe sleep|spin <ms>\n");
        return 2;
    }
    unsigned long ms = parse_ms(argv[2]);
    unsigned long start = uptime_us();
    if (kstreq(argv[1], "sleep")) {
        msleep(ms);
        unsigned long took = (uptime_us() - start) / 1000;
        kputs("schedprobe: slept ");
        kput_int((long)took);
        kputs(" ms\n");
        return took >= ms ? 0 : 1;
    }
    if (kstreq(argv[1], "spin")) {
        while (uptime_us() - start < ms * 1000) {
        }
        kputs("schedprobe: spun ");
        kput_int((long)((uptime_us() - start) / 1000));
        kputs(" ms\n");
        return 0;
    }
    kputs("usage: schedprobe sleep|spin <ms>\n");
    return 2;
}
