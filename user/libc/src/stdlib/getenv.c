// SPDX-License-Identifier: GPL-3.0-or-later
//
// getenv (C11 7.22.4.6). KoraOS passes no environment to a program yet, so no
// variable exists; returning NULL is the standard answer for that.

#include <stdlib.h>

char *getenv(const char *name) {
    (void)name;
    return NULL;
}
