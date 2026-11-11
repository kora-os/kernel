// SPDX-License-Identifier: GPL-3.0-or-later
//
// The storage behind errno. One variable for the whole program until the
// kernel keeps a per-thread pointer (TPIDR_EL0); callers already go through
// __errno_location(), so making it per thread will not change the ABI.

#include <errno.h>

static int errno_value;

int *__errno_location(void) {
    return &errno_value;
}
