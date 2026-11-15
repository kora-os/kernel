// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for forbid()/permit() (the big kernel lock from EL0).
//   forbidprobe          nesting and return values; then, with another task
//                        runnable, the longest stretch this task loses the CPU
//                        for, outside and inside forbid() (inside it must not
//                        be switched out); and that blocking breaks a forbid
//                        only while asleep.
//   forbidprobe hold <ms>  stay forbidden, spinning, for ms (for `tasks`).
#include "../../user/libk/koraos.h"

// Longest gap between consecutive clock reads over `ms`: the time slices
// another task got, if this one was preempted.
static unsigned long longest_gap_us(unsigned long ms) {
    unsigned long start = uptime_us();
    unsigned long last = start;
    unsigned long longest = 0;
    while (last - start < ms * 1000) {
        unsigned long now = uptime_us();
        if (now - last > longest) {
            longest = now - last;
        }
        last = now;
    }
    return longest;
}

static int fail(const char *what, int code) {
    fprintf(stderr, "forbidprobe: %s\n", what);
    return code;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "hold") == 0) {
        unsigned long ms = 0;
        for (const char *p = argv[2]; *p >= '0' && *p <= '9'; p++) {
            ms = ms * 10 + (unsigned long)(*p - '0');
        }
        forbid();
        puts("forbidprobe: holding");
        longest_gap_us(ms);
        permit();
        puts("forbidprobe: released");
        return 0;
    }

    if (permit() != -1) {
        return fail("permit() without forbid() did not fail", 1);
    }
    if (forbid() != 1 || forbid() != 2 || permit() != 1) {
        return fail("forbid() does not nest", 2);
    }
    // Still forbidden (depth 1): sleeping lets others run, then resumes it.
    msleep(30);
    if (permit() != 0) {
        return fail("forbid() depth lost across msleep()", 3);
    }

    unsigned long free_gap = longest_gap_us(300);
    forbid();
    unsigned long forbidden_gap = longest_gap_us(300);
    permit();
    // One line: stdout writes it whole at the newline, and tasks on other
    // cores print at the same time.
    printf("forbidprobe: longest gap free %lu us, forbidden %lu us\n", free_gap, forbidden_gap);
    return 0;
}
