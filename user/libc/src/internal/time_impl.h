// SPDX-License-Identifier: GPL-3.0-or-later
//
// Shared by src/time/*.c.
#ifndef LIBC_TIME_IMPL_H
#define LIBC_TIME_IMPL_H

#include <time.h>

// The generic timer's counter and its frequency in Hz (src/time/counter.c;
// host tests supply their own).
void __time_counter(unsigned long *count, unsigned long *frequency);

// Seconds since the epoch to broken-down UTC. 0, or -1 if the year does not
// fit tm_year.
int __secs_to_tm(long long secs, struct tm *tm);
// Broken-down time, with fields in any range, to seconds since the epoch.
long long __tm_to_secs(const struct tm *tm);

#endif // LIBC_TIME_IMPL_H
