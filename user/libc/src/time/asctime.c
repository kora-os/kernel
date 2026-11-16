// SPDX-License-Identifier: GPL-3.0-or-later
//
// asctime and ctime (C11 7.27.3.1, 7.27.3.2): "Sun Sep 16 01:03:52 1973\n"
// in a static array. The standard only defines the result for years of up
// to four digits; the array here has room for any year an int holds, and a
// field that is no day or month prints as "?".

#include <time.h>

static char text[48];

char *asctime(const struct tm *timeptr) {
    if (strftime(text, sizeof text, "%a %b %e %H:%M:%S %Y\n", timeptr) == 0) {
        text[0] = '\0';
    }
    return text;
}

char *ctime(const time_t *timer) {
    const struct tm *tm = localtime(timer);
    return tm != NULL ? asctime(tm) : NULL;
}
