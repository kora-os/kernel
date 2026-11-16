// SPDX-License-Identifier: GPL-3.0-or-later
//
// strftime (C11 7.27.3.5) in the "C" locale, the only one there is: English
// names, %c as "Sun Sep 16 01:03:52 1973", %x as "09/16/73". The time zone
// is always UTC (%z gives "+0000", %Z "UTC"). The E and O modifiers select
// alternative representations, of which the "C" locale has none, so they
// are accepted and ignored. Fields outside their range print as "?" where a
// name is wanted and as the number they are elsewhere.
//
// Numbers are formatted here, not by snprintf: a program that prints a date
// should not link the whole printf engine for it. asctime and ctime are
// strftime with a fixed format.

#include <time.h>

struct output {
    char *s;
    size_t room;  // bytes left, including the one for the NUL
    size_t length;
    int full;
};

static void put(struct output *out, char c) {
    if (out->room > 1) {
        *out->s++ = c;
        out->room--;
        out->length++;
    } else {
        out->full = 1;
    }
}

static void put_string(struct output *out, const char *s) {
    while (*s != '\0') {
        put(out, *s++);
    }
}

// A number in at least `width` characters, padded on the left with `pad`.
static void put_number(struct output *out, long long value, int width, char pad) {
    char digits[24];
    int n = 0;
    unsigned long long magnitude = value < 0 ? 0 - (unsigned long long)value : (unsigned long long)value;
    do {
        digits[n++] = (char)('0' + magnitude % 10);
        magnitude /= 10;
    } while (magnitude != 0);
    if (value < 0) {
        put(out, '-');
        width--;
    }
    for (; width > n; width--) {
        put(out, pad);
    }
    while (n > 0) {
        put(out, digits[--n]);
    }
}

static const char *const day_names[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                         "Thursday", "Friday", "Saturday"};
static const char *const month_names[12] = {"January",   "February", "March",    "April",
                                            "May",       "June",     "July",     "August",
                                            "September", "October",  "November", "December"};

// A name, or its first three letters; "?" if the index is no day or month.
static void put_name(struct output *out, const char *const *names, int count, int index,
                     int abbreviated) {
    if (index < 0 || index >= count) {
        put(out, '?');
        return;
    }
    const char *name = names[index];
    for (int i = 0; name[i] != '\0' && !(abbreviated && i == 3); i++) {
        put(out, name[i]);
    }
}

static int is_leap(long long year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int positive_mod(long long a, int b) {
    return (int)(a % b + (a % b < 0 ? b : 0));
}

// The ISO 8601 week (1 to 53) the day belongs to, and the year that week
// belongs to: weeks start on Monday, and week 1 is the one with the year's
// first Thursday, so the first days of January may belong to the last week
// of the year before, and the last days of December to week 1 of the next.
static int iso_week(const struct tm *tm, long long *iso_year) {
    long long year = (long long)tm->tm_year + 1900;
    int weekday = positive_mod(tm->tm_wday + 6, 7);                      // Monday = 0
    int january_1 = positive_mod((long long)tm->tm_wday - tm->tm_yday, 7);  // Sunday = 0
    int week = (tm->tm_yday - weekday + 10) / 7;
    if (week < 1) {
        // The last week of the previous year, which has 53 weeks if it began
        // on a Thursday, or on a Wednesday and was a leap year.
        year--;
        int previous_january_1 = positive_mod(january_1 - 365 - is_leap(year), 7);
        week = previous_january_1 == 4 || (previous_january_1 == 3 && is_leap(year)) ? 53 : 52;
    } else if (week == 53) {
        int long_year = january_1 == 4 || (january_1 == 3 && is_leap(year));
        if (!long_year) {
            year++;
            week = 1;
        }
    }
    *iso_year = year;
    return week;
}

static void format(struct output *out, const char *f, const struct tm *tm) {
    long long year = (long long)tm->tm_year + 1900;
    long long iso_year;
    for (; *f != '\0'; f++) {
        if (*f != '%') {
            put(out, *f);
            continue;
        }
        f++;
        if (*f == 'E' || *f == 'O') {
            f++;
        }
        switch (*f) {
        case 'a':
            put_name(out, day_names, 7, tm->tm_wday, 1);
            break;
        case 'A':
            put_name(out, day_names, 7, tm->tm_wday, 0);
            break;
        case 'b':
        case 'h':
            put_name(out, month_names, 12, tm->tm_mon, 1);
            break;
        case 'B':
            put_name(out, month_names, 12, tm->tm_mon, 0);
            break;
        case 'c':
            format(out, "%a %b %e %H:%M:%S %Y", tm);
            break;
        case 'C':
            put_number(out, year / 100 - (year % 100 < 0), 2, '0');
            break;
        case 'd':
            put_number(out, tm->tm_mday, 2, '0');
            break;
        case 'D':
            format(out, "%m/%d/%y", tm);
            break;
        case 'e':
            put_number(out, tm->tm_mday, 2, ' ');
            break;
        case 'F':
            format(out, "%Y-%m-%d", tm);
            break;
        case 'g':
            iso_week(tm, &iso_year);
            put_number(out, positive_mod(iso_year, 100), 2, '0');
            break;
        case 'G':
            iso_week(tm, &iso_year);
            put_number(out, iso_year, 1, '0');
            break;
        case 'H':
            put_number(out, tm->tm_hour, 2, '0');
            break;
        case 'I':
            put_number(out, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2, '0');
            break;
        case 'j':
            put_number(out, (long long)tm->tm_yday + 1, 3, '0');
            break;
        case 'm':
            put_number(out, (long long)tm->tm_mon + 1, 2, '0');
            break;
        case 'M':
            put_number(out, tm->tm_min, 2, '0');
            break;
        case 'n':
            put(out, '\n');
            break;
        case 'p':
            put_string(out, tm->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'r':
            format(out, "%I:%M:%S %p", tm);
            break;
        case 'R':
            format(out, "%H:%M", tm);
            break;
        case 'S':
            put_number(out, tm->tm_sec, 2, '0');
            break;
        case 't':
            put(out, '\t');
            break;
        case 'T':
            format(out, "%H:%M:%S", tm);
            break;
        case 'u':
            put_number(out, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0');
            break;
        case 'U':  // weeks starting on Sunday; days before the first one are week 0
            put_number(out, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0');
            break;
        case 'V':
            put_number(out, iso_week(tm, &iso_year), 2, '0');
            break;
        case 'w':
            put_number(out, tm->tm_wday, 1, '0');
            break;
        case 'W':  // weeks starting on Monday
            put_number(out, (tm->tm_yday + 7 - positive_mod(tm->tm_wday + 6, 7)) / 7, 2, '0');
            break;
        case 'x':
            format(out, "%m/%d/%y", tm);
            break;
        case 'X':
            format(out, "%H:%M:%S", tm);
            break;
        case 'y':
            put_number(out, positive_mod(year, 100), 2, '0');
            break;
        case 'Y':
            put_number(out, year, 1, '0');
            break;
        case 'z':
            put_string(out, "+0000");
            break;
        case 'Z':
            put_string(out, "UTC");
            break;
        case '%':
            put(out, '%');
            break;
        case '\0':
            put(out, '%');  // a lone % at the end
            return;
        default:
            put(out, '%');  // not a conversion: as written
            put(out, *f);
            break;
        }
    }
}

size_t strftime(char *restrict s, size_t maxsize, const char *restrict fmt,
                const struct tm *restrict timeptr) {
    struct output out = {s, maxsize, 0, maxsize == 0};
    format(&out, fmt, timeptr);
    if (out.full) {
        return 0;  // it did not fit: the array's contents are indeterminate
    }
    *out.s = '\0';
    return out.length;
}
