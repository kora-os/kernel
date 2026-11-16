// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for <time.h> (src/time).
//
// 1. By hand: known dates, the ends of the range a struct tm can hold,
//    mktime's normalization of fields out of range, every strftime
//    conversion on the standard's own example, ISO 8601 weeks at the turn of
//    the year, short buffers, asctime and ctime, difftime, and time() and
//    timespec_get() reporting that there is no wall clock.
// 2. Differential, against the host's gmtime_r, timegm, strftime and
//    asctime_r: 200 000 random times over two million years, 100 000 random
//    sets of fields in and out of range, and every strftime conversion on
//    every day of 32 years and on random dates of the years 1000 to 9999.
// 3. clock() over a counter the test supplies (src/time/counter.c reads the
//    AArch64 generic timer).

#include "test.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

// The host's struct tm (glibc and Apple's agree) and functions, bound by
// symbol name: host_rename.h renames the plain names to the libc's.
struct host_tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst;
    long tm_gmtoff;
    const char *tm_zone;
};
#define HOST(name) __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) name)
struct host_tm *host_gmtime_r(const long *timer, struct host_tm *result) HOST("gmtime_r");
long host_timegm(struct host_tm *tm) HOST("timegm");
size_t host_strftime(char *s, size_t max, const char *format, const struct host_tm *tm)
    HOST("strftime");
char *host_asctime_r(const struct host_tm *tm, char *buf) HOST("asctime_r");

static unsigned long fake_count, fake_frequency;

void __time_counter(unsigned long *count, unsigned long *frequency);
void __time_counter(unsigned long *count, unsigned long *frequency) {
    *count = fake_count;
    *frequency = fake_frequency;
}

static unsigned long long rng = 0xda942042e4dd58b5ull;

static unsigned long long next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

static long random_between(long lo, long hi) {
    return lo + (long)(next_random() % (unsigned long long)(hi - lo + 1));
}

static int fields_are(const struct tm *tm, int year, int mon, int mday, int hour, int min, int sec,
                      int wday, int yday) {
    return tm != NULL && tm->tm_year == year - 1900 && tm->tm_mon == mon - 1 &&
           tm->tm_mday == mday && tm->tm_hour == hour && tm->tm_min == min && tm->tm_sec == sec &&
           tm->tm_wday == wday && tm->tm_yday == yday && tm->tm_isdst == 0;
}

static int same_fields(const struct tm *a, const struct host_tm *b) {
    return a->tm_year == b->tm_year && a->tm_mon == b->tm_mon && a->tm_mday == b->tm_mday &&
           a->tm_hour == b->tm_hour && a->tm_min == b->tm_min && a->tm_sec == b->tm_sec &&
           a->tm_wday == b->tm_wday && a->tm_yday == b->tm_yday;
}

static void test_header(void) {
    CHECK(sizeof(time_t) == 8 && (time_t)-1 < 0, "time_t is 64 bits and signed");
    CHECK(sizeof(clock_t) == 8 && CLOCKS_PER_SEC == 1000000, "clock_t counts microseconds");
    struct timespec ts = {5, 999999999};
    CHECK(ts.tv_sec == 5 && ts.tv_nsec == 999999999 && TIME_UTC != 0, "struct timespec");
}

static void test_no_wall_clock(void) {
    time_t t = 5;
    CHECK(time(NULL) == (time_t)-1, "time() has no calendar time to give");
    CHECK(time(&t) == (time_t)-1 && t == (time_t)-1, "and stores that too");
    struct timespec ts = {7, 7};
    CHECK(timespec_get(&ts, TIME_UTC) == 0 && ts.tv_sec == 7, "timespec_get fails");
}

static void test_difftime(void) {
    CHECK(difftime(10, 4) == 6.0 && difftime(4, 10) == -6.0 && difftime(0, 0) == 0.0, "difftime");
    CHECK(difftime(LONG_MAX, LONG_MAX - 3) == 3.0, "exact for large times close together");
    CHECK(difftime(LONG_MAX, LONG_MIN) == 0x1p64, "and no overflow for times far apart");
    CHECK(difftime(LONG_MIN, 1) == -0x1p63, "at the other end");
}

static void test_clock(void) {
    fake_frequency = 62500000;  // QEMU's
    fake_count = 0;
    CHECK(clock() == 0, "clock at boot");
    fake_count = 62500000;
    CHECK(clock() == 1000000, "one second is CLOCKS_PER_SEC");
    fake_count = 62500;
    CHECK(clock() == 1000, "a millisecond");
    fake_frequency = 19200000;  // a Pi 3's
    fake_count = 19200000ul * 86400 * 365 * 10 + 9600000;
    CHECK(clock() == 86400l * 365 * 10 * 1000000 + 500000, "ten years of uptime do not overflow");
    fake_frequency = 54000000;
    fake_count = 27;
    CHECK(clock() == 0, "less than a microsecond");
    fake_count = 54;
    CHECK(clock() == 1, "a microsecond");
    fake_frequency = 0;
    CHECK(clock() == (clock_t)-1, "no counter frequency: not available");
}

static void test_gmtime(void) {
    time_t t = 0;
    CHECK(fields_are(gmtime(&t), 1970, 1, 1, 0, 0, 0, 4, 0), "the epoch was a Thursday");
    t = -1;
    CHECK(fields_are(gmtime(&t), 1969, 12, 31, 23, 59, 59, 3, 364), "one second before it");
    t = 86399;
    CHECK(fields_are(gmtime(&t), 1970, 1, 1, 23, 59, 59, 4, 0), "the last second of that day");
    t = 1000000000;
    CHECK(fields_are(gmtime(&t), 2001, 9, 9, 1, 46, 40, 0, 251), "a thousand million seconds");
    t = 951782400;
    CHECK(fields_are(gmtime(&t), 2000, 2, 29, 0, 0, 0, 2, 59), "2000 was a leap year");
    t = 4107542400;
    CHECK(fields_are(gmtime(&t), 2100, 3, 1, 0, 0, 0, 1, 59), "2100 will not be one");
    t = 2147483648;
    CHECK(fields_are(gmtime(&t), 2038, 1, 19, 3, 14, 8, 2, 18), "past 31 bits of seconds");
    t = -62135596800;
    CHECK(fields_are(gmtime(&t), 1, 1, 1, 0, 0, 0, 1, 0), "the first day of year 1, a Monday");
    t = -62167219200;
    CHECK(fields_are(gmtime(&t), 0, 1, 1, 0, 0, 0, 6, 0), "year 0 exists, and is a leap year");
    t = -62167219200 + 59 * 86400;
    CHECK(fields_are(gmtime(&t), 0, 2, 29, 0, 0, 0, 2, 59), "with its February 29");
    t = 253402300799;
    CHECK(fields_are(gmtime(&t), 9999, 12, 31, 23, 59, 59, 5, 364), "the last second of 9999");

    struct tm *shared = gmtime(&t);
    CHECK(localtime(&t) == shared && fields_are(localtime(&t), 9999, 12, 31, 23, 59, 59, 5, 364),
          "localtime is gmtime: there are no time zones");

    // The range ends where the year stops fitting tm_year.
    struct tm last = {59, 59, 23, 31, 11, INT_MAX, 0, 0, 0};
    time_t end = mktime(&last);
    CHECK(end != (time_t)-1 && last.tm_year == INT_MAX && last.tm_yday == 364, "the last second");
    struct tm *tm = gmtime(&end);
    CHECK(tm != NULL && tm->tm_year == INT_MAX && tm->tm_mon == 11 && tm->tm_mday == 31,
          "gmtime reaches it");
    end++;
    errno = 0;
    CHECK(gmtime(&end) == NULL && errno == EOVERFLOW, "and fails one second later");
    struct tm first = {0, 0, 0, 1, 0, INT_MIN, 0, 0, 0};
    time_t begin = mktime(&first);
    tm = gmtime(&begin);
    CHECK(tm != NULL && tm->tm_year == INT_MIN && tm->tm_mon == 0 && tm->tm_mday == 1,
          "the first second");
    begin--;
    CHECK(gmtime(&begin) == NULL, "and one before it");
    t = LONG_MAX;
    CHECK(gmtime(&t) == NULL && localtime(&t) == NULL && ctime(&t) == NULL,
          "the largest time_t is out of range");
    t = LONG_MIN;
    CHECK(gmtime(&t) == NULL, "and the smallest");
}

static void test_gmtime_against_host(void) {
    unsigned mismatches = 0;
    for (int i = 0; i < 200000; i++) {
        long t;
        switch (i % 4) {
        case 0:
            t = random_between(-(1l << 45), 1l << 45);  // a million years either way
            break;
        case 1:
            t = random_between(-(1l << 33), 1l << 33);  // 1700 to 2240
            break;
        case 2:
            t = random_between(-400, 400) * 86400 * 365 + random_between(-2, 2);  // year ends
            break;
        default:
            t = random_between(0, 1l << 31);
            break;
        }
        struct host_tm want;
        struct tm *got = gmtime(&t);
        test_checks++;
        if (host_gmtime_r(&t, &want) == NULL || got == NULL || !same_fields(got, &want)) {
            test_failures++;
            if (++mismatches <= 10) {
                test_printf("FAIL gmtime(%ld): %d-%d-%d, host %d-%d-%d\n", t,
                            got ? got->tm_year + 1900 : 0, got ? got->tm_mon + 1 : 0,
                            got ? got->tm_mday : 0, want.tm_year + 1900, want.tm_mon + 1,
                            want.tm_mday);
            }
        }
    }
}

static void test_mktime(void) {
    struct tm tm = {40, 46, 1, 9, 8, 101, -1, -1, 1};
    CHECK(mktime(&tm) == 1000000000 && tm.tm_wday == 0 && tm.tm_yday == 251 && tm.tm_isdst == 0,
          "mktime sets the day of the week, the day of the year and tm_isdst");
    struct tm epoch = {0, 0, 0, 1, 0, 70, 0, 0, 0};
    CHECK(mktime(&epoch) == 0, "the epoch");
    struct tm before = {59, 59, 23, 31, 11, 69, 0, 0, 0};
    CHECK(mktime(&before) == -1 && before.tm_wday == 3, "the second before it is -1, validly");

    // Fields out of range are carried over.
    struct tm month13 = {0, 0, 0, 1, 12, 100, 0, 0, 0};
    CHECK(mktime(&month13) == 978307200 && month13.tm_year == 101 && month13.tm_mon == 0,
          "month 12 is January of the next year");
    struct tm month_before = {0, 0, 0, 1, -1, 100, 0, 0, 0};
    mktime(&month_before);
    CHECK(month_before.tm_year == 99 && month_before.tm_mon == 11, "month -1 is last December");
    struct tm day0 = {0, 0, 0, 0, 2, 100, 0, 0, 0};
    mktime(&day0);
    CHECK(day0.tm_mon == 1 && day0.tm_mday == 29, "March 0 is the last day of February");
    struct tm day366 = {0, 0, 0, 366, 0, 100, 0, 0, 0};
    mktime(&day366);
    CHECK(day366.tm_mon == 11 && day366.tm_mday == 31 && day366.tm_yday == 365,
          "January 366 of a leap year is December 31");
    struct tm second60 = {60, 59, 23, 31, 11, 98, 0, 0, 0};
    mktime(&second60);
    CHECK(second60.tm_year == 99 && second60.tm_mon == 0 && second60.tm_mday == 1 &&
              second60.tm_sec == 0 && second60.tm_hour == 0,
          "second 60 is the next minute");
    struct tm negative = {-1, -1, -1, 1, 0, 100, 0, 0, 0};
    mktime(&negative);
    CHECK(negative.tm_year == 99 && negative.tm_mday == 31 && negative.tm_hour == 22 &&
              negative.tm_min == 58 && negative.tm_sec == 59,
          "negative fields go backwards");
    struct tm far = {INT_MAX, INT_MAX, INT_MAX, INT_MAX, INT_MAX, 0, 0, 0, 0};
    time_t t = mktime(&far);
    struct tm *back = gmtime(&t);
    CHECK(t != (time_t)-1 && back != NULL && back->tm_year == far.tm_year &&
              back->tm_yday == far.tm_yday,
          "the largest fields still add up");

    struct tm too_late = {0, 0, 0, 1, 0, INT_MAX, 0, 0, 0};
    too_late.tm_mon = 12;  // January of the year after the last one
    errno = 0;
    CHECK(mktime(&too_late) == (time_t)-1 && errno == EOVERFLOW && too_late.tm_mon == 12,
          "a year past INT_MAX fails and leaves the fields alone");
}

static void test_mktime_against_host(void) {
    unsigned mismatches = 0;
    for (int i = 0; i < 100000; i++) {
        struct tm tm;
        // Years from 1910 on: Apple's timegm refuses those before 1900 (the
        // round trips below cover the rest of the range).
        if (i % 2 == 0) {  // in range
            tm.tm_year = (int)random_between(10, 8000);
            tm.tm_mon = (int)random_between(0, 11);
            tm.tm_mday = (int)random_between(1, 28);
            tm.tm_hour = (int)random_between(0, 23);
            tm.tm_min = (int)random_between(0, 59);
            tm.tm_sec = (int)random_between(0, 59);
        } else {  // out of range, in both directions
            tm.tm_year = (int)random_between(10, 8000);
            tm.tm_mon = (int)random_between(-40, 50);
            tm.tm_mday = (int)random_between(-400, 800);
            tm.tm_hour = (int)random_between(-100, 100);
            tm.tm_min = (int)random_between(-200, 200);
            tm.tm_sec = (int)random_between(-100000, 100000);
        }
        tm.tm_wday = tm.tm_yday = -5;
        tm.tm_isdst = 0;
        struct host_tm host = {tm.tm_sec,  tm.tm_min,  tm.tm_hour, tm.tm_mday, tm.tm_mon,
                               tm.tm_year, -5,         -5,         0,          0,
                               NULL};
        long want = host_timegm(&host);
        long got = mktime(&tm);
        test_checks++;
        if (want != got || !same_fields(&tm, &host)) {
            test_failures++;
            if (++mismatches <= 10) {
                test_printf("FAIL mktime: %ld, host %ld (%d-%d-%d, host %d-%d-%d)\n", got, want,
                            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, host.tm_year + 1900,
                            host.tm_mon + 1, host.tm_mday);
            }
        }
    }
}

// mktime is the inverse of gmtime, which the host has confirmed over two
// million years: any time comes back from its own fields, and fields out of
// range come back as the fields of the time they add up to.
static void test_mktime_round_trips(void) {
    unsigned mismatches = 0;
    for (int i = 0; i < 100000; i++) {
        long t = i % 2 ? random_between(-(1l << 45), 1l << 45)
                       : random_between(LONG_MIN / 300, LONG_MAX / 300);
        struct tm tm = *gmtime(&t);
        struct tm shifted = tm;
        int months = (int)random_between(-5000, 5000), minutes = (int)random_between(-100000, 100000);
        // The same moment, written with a month and a minute out of range.
        shifted.tm_year -= months / 12;
        shifted.tm_mon += months / 12 * 12;
        shifted.tm_hour -= minutes / 60;
        shifted.tm_min += minutes / 60 * 60;
        shifted.tm_wday = shifted.tm_yday = -1;
        test_checks++;
        if (mktime(&tm) != t || mktime(&shifted) != t || memcmp(&tm, &shifted, sizeof tm) != 0 ||
            memcmp(&tm, gmtime(&t), sizeof tm) != 0) {
            test_failures++;
            if (++mismatches <= 10) {
                test_printf("FAIL mktime round trip of %ld: %ld, %ld\n", t, (long)mktime(&tm),
                            (long)mktime(&shifted));
            }
        }
    }
}

static const char *format_time(const char *format, const struct tm *tm) {
    static char buf[256];
    size_t n = strftime(buf, sizeof buf, format, tm);
    return n == strlen(buf) ? buf : "(wrong length)";
}

#define FORMATS_TO(format, want) \
    CHECK(strcmp(format_time(format, tm), want) == 0, "%s gives \"%s\"", format, format_time(format, tm))

static void test_strftime(void) {
    // The standard's example: Sunday, September 16, 1973, 01:03:52.
    time_t t = 116989432;
    const struct tm *tm = gmtime(&t);
    FORMATS_TO("%a", "Sun");
    FORMATS_TO("%A", "Sunday");
    FORMATS_TO("%b", "Sep");
    FORMATS_TO("%B", "September");
    FORMATS_TO("%c", "Sun Sep 16 01:03:52 1973");
    FORMATS_TO("%C", "19");
    FORMATS_TO("%d", "16");
    FORMATS_TO("%D", "09/16/73");
    FORMATS_TO("%e", "16");
    FORMATS_TO("%F", "1973-09-16");
    FORMATS_TO("%g", "73");
    FORMATS_TO("%G", "1973");
    FORMATS_TO("%h", "Sep");
    FORMATS_TO("%H", "01");
    FORMATS_TO("%I", "01");
    FORMATS_TO("%j", "259");
    FORMATS_TO("%m", "09");
    FORMATS_TO("%M", "03");
    FORMATS_TO("%n", "\n");
    FORMATS_TO("%p", "AM");
    FORMATS_TO("%r", "01:03:52 AM");
    FORMATS_TO("%R", "01:03");
    FORMATS_TO("%S", "52");
    FORMATS_TO("%t", "\t");
    FORMATS_TO("%T", "01:03:52");
    FORMATS_TO("%u", "7");
    FORMATS_TO("%U", "37");
    FORMATS_TO("%V", "37");
    FORMATS_TO("%w", "0");
    FORMATS_TO("%W", "37");
    FORMATS_TO("%x", "09/16/73");
    FORMATS_TO("%X", "01:03:52");
    FORMATS_TO("%y", "73");
    FORMATS_TO("%Y", "1973");
    FORMATS_TO("%z", "+0000");
    FORMATS_TO("%Z", "UTC");
    FORMATS_TO("%%", "%");
    FORMATS_TO("%Ec %EC %Ex %EX %Ey %EY", "Sun Sep 16 01:03:52 1973 19 09/16/73 01:03:52 73 1973");
    FORMATS_TO("%Od %Oe %OH %OI %Om %OM %OS %Ou %OU %OV %Ow %OW %Oy", "16 16 01 01 09 03 52 7 37 37 0 37 73");
    FORMATS_TO("Date: %Y-%m-%dT%H:%M:%SZ!", "Date: 1973-09-16T01:03:52Z!");
    FORMATS_TO("", "");
    FORMATS_TO("100%", "100%");
    FORMATS_TO("%q%1", "%q%1");

    // Midnight, noon and a single-digit day.
    t = 946684800 + 4 * 86400;  // 2000-01-05 00:00:00, a Wednesday
    tm = gmtime(&t);
    FORMATS_TO("%I %p %e|%d|%j|%u|%w", "12 AM  5|05|005|3|3");
    t += 12 * 3600;
    tm = gmtime(&t);
    FORMATS_TO("%I %p %H", "12 PM 12");
    t += 3600;
    tm = gmtime(&t);
    FORMATS_TO("%I %p %H %r", "01 PM 13 01:00:00 PM");

    // Years with other than four digits.
    struct tm odd = {0, 0, 0, 1, 0, 0, 0, 0, 0};
    odd.tm_year = 12345 - 1900;
    tm = &odd;
    FORMATS_TO("%Y %C %y", "12345 123 45");
    odd.tm_year = 5 - 1900;
    FORMATS_TO("%Y|%C|%y", "5|00|05");
    odd.tm_year = -42 - 1900;
    FORMATS_TO("%Y|%C|%y", "-42|-1|58");
    odd.tm_year = INT_MAX;
    FORMATS_TO("%Y", "2147485547");
    odd.tm_year = INT_MIN;
    FORMATS_TO("%Y", "-2147481748");

    // Fields that are no day or month.
    struct tm bad = {0, 0, 0, 1, 12, 100, 7, 0, 0};
    tm = &bad;
    FORMATS_TO("%a %A %b %B", "? ? ? ?");
    bad.tm_wday = -1, bad.tm_mon = -1;
    FORMATS_TO("%a %b", "? ?");
}

static void test_strftime_sizes(void) {
    time_t t = 116989432;
    const struct tm *tm = gmtime(&t);
    char buf[32];
    memset(buf, '#', sizeof buf);
    CHECK(strftime(buf, 11, "%F", tm) == 10 && strcmp(buf, "1973-09-16") == 0 && buf[11] == '#',
          "a buffer of exactly the size");
    CHECK(strftime(buf, 10, "%F", tm) == 0, "one byte short: 0");
    CHECK(strftime(buf, 1, "%F", tm) == 0 && strftime(buf, 0, "%F", tm) == 0, "no room at all");
    CHECK(strftime(buf, 1, "", tm) == 0 && buf[0] == '\0', "an empty result fits one byte");
    CHECK(strftime(buf, 0, "", tm) == 0, "but not none");
    memset(buf, '#', sizeof buf);
    CHECK(strftime(buf, 5, "%Y%%", tm) == 0 && buf[5] == '#', "nothing written past the size");
}

// The week of the year, where the turn of the year makes it interesting.
static void test_iso_weeks(void) {
    static const struct {
        int year, mon, mday;
        const char *want;  // %G-W%V-%u %U %W
    } cases[] = {
        {2005, 1, 1, "2004-W53-6 00 00"},  {2005, 1, 2, "2004-W53-7 01 00"},
        {2005, 1, 3, "2005-W01-1 01 01"},  {2005, 12, 31, "2005-W52-6 52 52"},
        {2007, 1, 1, "2007-W01-1 00 01"},  {2007, 12, 30, "2007-W52-7 52 52"},
        {2007, 12, 31, "2008-W01-1 52 53"}, {2008, 12, 28, "2008-W52-7 52 51"},
        {2008, 12, 29, "2009-W01-1 52 52"}, {2009, 12, 31, "2009-W53-4 52 52"},
        {2010, 1, 3, "2009-W53-7 01 00"},  {2010, 1, 4, "2010-W01-1 01 01"},
        {2020, 12, 31, "2020-W53-4 52 52"}, {2021, 1, 3, "2020-W53-7 01 00"},
        {2021, 1, 4, "2021-W01-1 01 01"},  {2016, 1, 3, "2015-W53-7 01 00"},
        {1999, 1, 3, "1998-W53-7 01 00"},  {2000, 1, 2, "1999-W52-7 01 00"},
        {2012, 12, 31, "2013-W01-1 53 53"}, {2015, 12, 31, "2015-W53-4 52 52"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        struct tm tm = {0, 0, 12, cases[i].mday, cases[i].mon - 1, cases[i].year - 1900, 0, 0, 0};
        mktime(&tm);
        char buf[32];
        strftime(buf, sizeof buf, "%G-W%V-%u %U %W", &tm);
        CHECK(strcmp(buf, cases[i].want) == 0, "%d-%02d-%02d is %s: got %s", cases[i].year,
              cases[i].mon, cases[i].mday, cases[i].want, buf);
    }
}

// Every conversion but %Z (the hosts call the zone "UTC" or "GMT").
#define ALL_CONVERSIONS \
    "%a|%A|%b|%B|%c|%C|%d|%D|%e|%F|%g|%G|%h|%H|%I|%j|%m|%M|%n|%p|%r|%R|%S|%t|%T|%u|%U|%V|%w|" \
    "%W|%x|%X|%y|%Y|%z|%%|%Ec|%EC|%Ex|%EX|%Ey|%EY|%Od|%Oe|%OH|%OI|%Om|%OM|%OS|%Ou|%OU|%OV|%Ow|" \
    "%OW|%Oy"

static unsigned strftime_mismatches;

static void compare_strftime(long t) {
    struct host_tm host;
    host_gmtime_r(&t, &host);
    const struct tm *tm = gmtime(&t);
    char want[400], got[400], want_asc[64];
    size_t want_n = host_strftime(want, sizeof want, ALL_CONVERSIONS, &host);
    size_t got_n = strftime(got, sizeof got, ALL_CONVERSIONS, tm);
    host_asctime_r(&host, want_asc);
    test_checks++;
    if (want_n != got_n || strcmp(want, got) != 0 || strcmp(asctime(tm), want_asc) != 0) {
        test_failures++;
        if (++strftime_mismatches <= 5) {
            test_printf("FAIL strftime at %ld:\n libc %s\n host %s\n asctime %s host    %s", t, got,
                        want, asctime(tm), want_asc);
        }
    }
}

static void test_strftime_against_host(void) {
    // Every day from 1999 to 2030, at a time that moves through the day.
    for (long day = 10592; day < 10592 + 32 * 366; day++) {
        compare_strftime(day * 86400 + day * 7919 % 86400);
    }
    // Random moments of the years 1000 to 9999, where the hosts agree on how
    // a year is written.
    for (int i = 0; i < 100000; i++) {
        compare_strftime(random_between(-30610224000, 253402300799));
    }
}

static void test_asctime(void) {
    time_t t = 116989432;
    CHECK(strcmp(asctime(gmtime(&t)), "Sun Sep 16 01:03:52 1973\n") == 0, "the standard's example");
    CHECK(strcmp(ctime(&t), "Sun Sep 16 01:03:52 1973\n") == 0, "ctime is the same in UTC");
    t = 946684800 + 4 * 86400 + 5;
    CHECK(strcmp(ctime(&t), "Wed Jan  5 00:00:05 2000\n") == 0, "a day of one digit is padded");
    struct tm far = {0, 0, 0, 1, 0, INT_MAX, 1, 0, 0};
    CHECK(strcmp(asctime(&far), "Mon Jan  1 00:00:00 2147485547\n") == 0,
          "a year of ten digits still fits");
    struct tm bad = {0, 0, 0, 1, 99, 100, 99, 0, 0};
    CHECK(strcmp(asctime(&bad), "? ?  1 00:00:00 2000\n") == 0, "fields that are no day or month");
}

TEST_MAIN(test_header, test_no_wall_clock, test_difftime, test_clock, test_gmtime,
          test_gmtime_against_host, test_mktime, test_mktime_against_host, test_mktime_round_trips,
          test_strftime,
          test_strftime_sizes, test_iso_weeks, test_strftime_against_host, test_asctime)
