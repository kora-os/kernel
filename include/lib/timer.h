// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Busy-wait delays and a monotonic clock built on the ARMv8 generic timer
// (CNTPCT_EL0 / CNTFRQ_EL0). EL1 access to the physical counter is enabled in
// boot.S. The busy-wait `delay()` in utils.S counts CPU iterations and so has no
// defined real-time meaning; these helpers are wall-clock accurate and are what
// device bring-up (USB reset/enumeration timing) should use.

#ifdef __cplusplus
extern "C" {
#endif

// Timer tick frequency in Hz (CNTFRQ_EL0). Varies by platform (e.g. ~19.2 MHz on
// real Raspberry Pi, ~62.5 MHz under QEMU), so always read it at runtime.
uint64_t timer_freq_hz(void);

// Raw monotonic counter value (CNTPCT_EL0).
uint64_t timer_ticks(void);

// Monotonic uptime in microseconds since the counter started.
uint64_t timer_us(void);

// Busy-wait for at least the given number of microseconds / milliseconds.
void udelay(uint64_t us);
void mdelay(uint64_t ms);

#ifdef __cplusplus
}
#endif
