// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// A periodic timer tick driven by the ARMv8 generic timer (CNTP_EL0), delivered
// through KoraOS's interrupt controller (arch/irq.h). This is the first real
// interrupt source in KoraOS and the foundation a future preemptive scheduler
// will build on; for now it maintains a monotonic tick counter and proves the
// IRQ path end to end.

#ifdef __cplusplus
extern "C" {
#endif

// Start the periodic tick at `hz` interrupts per second. Requires irq_init()
// first and irq_enable() to actually fire.
void systick_init(unsigned hz);

// Ticks elapsed since systick_init().
uint64_t systick_count(void);

#ifdef __cplusplus
}
#endif
