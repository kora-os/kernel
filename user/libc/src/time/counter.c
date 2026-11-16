// SPDX-License-Identifier: GPL-3.0-or-later
//
// The ARM generic timer's virtual counter, which EL0 may read without a
// syscall, and its frequency (19.2 MHz on a Pi 3, 54 MHz on a Pi 4, 62.5 MHz
// on QEMU). The one piece of <time.h> that touches hardware.
//
// The libc is also compiled for the hosts that run the unit tests, and those
// are not all AArch64: elsewhere there is no counter, and clock() reports
// that processor time is not available. (The libc's own time test supplies
// a counter of its own in place of this file.)

#include "time_impl.h"

void __time_counter(unsigned long *count, unsigned long *frequency) {
#if defined(__aarch64__)
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(*count));
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(*frequency));
#else
    *count = 0;
    *frequency = 0;
#endif
}
