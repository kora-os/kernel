// SPDX-License-Identifier: GPL-3.0-or-later
//
// mktime, gmtime and localtime (C11 7.27.2.3, 7.27.3): seconds since
// 1970-01-01 00:00:00 UTC to and from broken-down time, in the proleptic
// Gregorian calendar, for the whole range a struct tm can hold (tm_year is
// an int, so about two thousand million years either way). No leap seconds,
// and no time zones: local time is UTC and tm_isdst is 0.
//
// The day arithmetic counts in 400-year eras of exactly 146097 days, with
// the year starting in March, so that the leap day is the last day of a
// year and month lengths follow one formula (Howard Hinnant's "days from
// civil" algorithms).

#include <errno.h>
#include <limits.h>
#include <time.h>

#include "time_impl.h"

#define SECONDS_PER_DAY 86400ll
#define DAYS_PER_ERA 146097ll
#define DAYS_TO_EPOCH 719468ll  // from 0000-03-01 to 1970-01-01

// Division and remainder rounding towards minus infinity.
static long long floor_div(long long a, long long b) {
    return a / b - (a % b < 0);
}

static long long floor_mod(long long a, long long b) {
    return a % b + (a % b < 0 ? b : 0);
}

// Days from 1970-01-01 to year-month-day (month 1 to 12, any day number).
static long long days_from_civil(long long year, int month, long long day) {
    year -= month <= 2;
    long long era = floor_div(year, 400);
    long long year_of_era = year - era * 400;                                // 0 to 399
    long long day_of_year = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5;  // of March 1
    long long day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return era * DAYS_PER_ERA + day_of_era - DAYS_TO_EPOCH + day - 1;
}

int __secs_to_tm(long long secs, struct tm *tm) {
    long long days = floor_div(secs, SECONDS_PER_DAY);
    // Not secs - days * 86400: for the most negative times that product is
    // below the 64-bit range.
    long long rest = floor_mod(secs, SECONDS_PER_DAY);  // 0 to 86399

    long long z = days + DAYS_TO_EPOCH;
    long long era = floor_div(z, DAYS_PER_ERA);
    long long day_of_era = z - era * DAYS_PER_ERA;  // 0 to 146096
    long long year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    long long day_of_year =  // counted from March 1: 0 to 365
        day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    long long month_from_march = (5 * day_of_year + 2) / 153;  // 0 to 11
    int day = (int)(day_of_year - (153 * month_from_march + 2) / 5) + 1;
    int month = (int)(month_from_march < 10 ? month_from_march + 3 : month_from_march - 9);
    long long year = year_of_era + era * 400 + (month <= 2);

    if (year - 1900 > INT_MAX || year - 1900 < INT_MIN) {
        return -1;
    }
    tm->tm_year = (int)(year - 1900);
    tm->tm_mon = month - 1;
    tm->tm_mday = day;
    tm->tm_hour = (int)(rest / 3600);
    tm->tm_min = (int)(rest / 60 % 60);
    tm->tm_sec = (int)(rest % 60);
    tm->tm_wday = (int)floor_mod(days + 4, 7);  // 1970-01-01 was a Thursday
    tm->tm_yday = (int)(days - days_from_civil(year, 1, 1));
    tm->tm_isdst = 0;
    return 0;
}

// Every field may be out of its range, in either direction: months carry
// into years, and days, hours, minutes and seconds are simply added up. With
// int fields the sum is far inside 64 bits.
long long __tm_to_secs(const struct tm *tm) {
    long long year = (long long)tm->tm_year + 1900 + floor_div(tm->tm_mon, 12);
    int month = (int)floor_mod(tm->tm_mon, 12) + 1;
    long long days = days_from_civil(year, month, tm->tm_mday);
    return days * SECONDS_PER_DAY + tm->tm_hour * 3600ll + tm->tm_min * 60ll + tm->tm_sec;
}

time_t mktime(struct tm *timeptr) {
    long long secs = __tm_to_secs(timeptr);
    struct tm normal;
    if (__secs_to_tm(secs, &normal) != 0) {
        errno = EOVERFLOW;
        return (time_t)-1;  // the year no longer fits tm_year
    }
    *timeptr = normal;
    return (time_t)secs;
}

// gmtime and localtime share one result, as the standard allows.
static struct tm result;

struct tm *gmtime(const time_t *timer) {
    if (__secs_to_tm(*timer, &result) != 0) {
        errno = EOVERFLOW;
        return NULL;
    }
    return &result;
}

struct tm *localtime(const time_t *timer) {
    return gmtime(timer);
}
