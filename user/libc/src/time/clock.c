// SPDX-License-Identifier: GPL-3.0-or-later
//
// clock() (C11 7.27.2.1): microseconds since boot. The standard asks for the
// processor time the program used; the kernel keeps no such account, so this
// is elapsed time, good for what clock() is used for: the difference of two
// readings around a piece of work.

#include <time.h>

#include "time_impl.h"

clock_t clock(void) {
    unsigned long count, frequency;
    __time_counter(&count, &frequency);
    if (frequency == 0) {
        return (clock_t)-1;
    }
    // Whole seconds and the rest separately: count * 1000000 would overflow
    // after a few days.
    unsigned long micros = count / frequency * 1000000ul + count % frequency * 1000000ul / frequency;
    return (clock_t)micros;
}
