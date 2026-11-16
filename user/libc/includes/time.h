// SPDX-License-Identifier: GPL-3.0-or-later
//
// <time.h> (C11 7.27). KoraOS has no wall clock yet: time() returns
// (time_t)-1 and timespec_get() returns 0, the answers the standard gives for
// "the calendar time is not available". Everything that computes with times
// works: mktime, gmtime, localtime, asctime, ctime, strftime, difftime.
//
// There are no time zones: local time is UTC, and tm_isdst is always 0.
// clock() counts microseconds since boot, from the generic timer: the kernel
// does not account processor time per program, so it is elapsed time.
#ifndef LIBC_TIME_H
#define LIBC_TIME_H

#include <stddef.h>

typedef long clock_t;
typedef long time_t;  // seconds since 1970-01-01 00:00:00 UTC, 64 bits

#define CLOCKS_PER_SEC 1000000L
#define TIME_UTC 1

struct timespec {
    time_t tv_sec;
    long tv_nsec;  // nanoseconds, 0 to 999999999
};

struct tm {
    int tm_sec;    // 0 to 60
    int tm_min;    // 0 to 59
    int tm_hour;   // 0 to 23
    int tm_mday;   // 1 to 31
    int tm_mon;    // months since January, 0 to 11
    int tm_year;   // years since 1900
    int tm_wday;   // days since Sunday, 0 to 6
    int tm_yday;   // days since January 1, 0 to 365
    int tm_isdst;  // daylight saving time: always 0
};

clock_t clock(void);
double difftime(time_t time1, time_t time0);
time_t mktime(struct tm *timeptr);
time_t time(time_t *timer);
int timespec_get(struct timespec *ts, int base);

char *asctime(const struct tm *timeptr);
char *ctime(const time_t *timer);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
size_t strftime(char *restrict s, size_t maxsize, const char *restrict format,
                const struct tm *restrict timeptr);

#endif // LIBC_TIME_H
