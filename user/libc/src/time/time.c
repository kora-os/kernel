// SPDX-License-Identifier: GPL-3.0-or-later
//
// time(), timespec_get() and difftime() (C11 7.27.2). The kernel has no wall
// clock yet (no RTC driver, and a Pi 3 or 4 has no RTC at all), so the
// calendar time is "not available": time() returns (time_t)-1 and
// timespec_get() returns 0.

#include <time.h>

time_t time(time_t *timer) {
    if (timer != NULL) {
        *timer = (time_t)-1;
    }
    return (time_t)-1;
}

int timespec_get(struct timespec *ts, int base) {
    (void)ts;
    (void)base;
    return 0;
}

double difftime(time_t time1, time_t time0) {
    // Exact when the difference fits 64 bits (then rounded once, to double);
    // otherwise the operands are far apart and rounding each first is as good.
    time_t difference;
    if (!__builtin_sub_overflow(time1, time0, &difference)) {
        return (double)difference;
    }
    return (double)time1 - (double)time0;
}
