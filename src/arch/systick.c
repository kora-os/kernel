// SPDX-License-Identifier: GPL-3.0-or-later
//
// Periodic system tick on the generic timer. See arch/systick.h.

#include "arch/systick.h"

#include "arch/irq.h"
#include "lib/timer.h"
#include "memory_access.h"
#include "peripherals/irq.h"

static volatile uint64_t g_ticks;
static uint64_t g_interval;  // timer ticks between interrupts
static uint64_t g_deadline;  // absolute CNTPCT value of the next tick
static void (*g_tick_hook)(void);

static inline uint64_t read_cntpct(void) {
    uint64_t v;
    asm volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

static inline void write_cntp_cval(uint64_t v) {
    asm volatile("msr cntp_cval_el0, %0" ::"r"(v));
}

static inline void write_cntp_ctl(uint64_t v) {
    asm volatile("msr cntp_ctl_el0, %0" ::"r"(v));
}

static void systick_isr(void *ctx) {
    (void)ctx;
    g_ticks++;
    // Advance the absolute deadline by one interval so periods do not drift with
    // interrupt latency (re-arming a relative TVAL would lose each overshoot).
    g_deadline += g_interval;
    write_cntp_cval(g_deadline);
    if (g_tick_hook != NULL) {
        g_tick_hook();
    }
}

void systick_set_tick_hook(void (*hook)(void)) {
    g_tick_hook = hook;
}

void systick_init(unsigned hz) {
    if (hz == 0) {
        return;
    }
    g_interval = timer_freq_hz() / hz;
    g_ticks = 0;

    irq_connect(IRQ_LOCAL_CNTPNS, systick_isr, NULL);

    // Route the non-secure physical timer event to this core's IRQ line.
    write32(CORE0_TIMER_IRQCNTL, LOCAL_TIMER_IRQ_CNTPNS);
    asm volatile("dsb sy" ::: "memory");

    // Arm the first absolute deadline and enable the timer (bit0=ENABLE,
    // bit1=IMASK=0).
    g_deadline = read_cntpct() + g_interval;
    write_cntp_cval(g_deadline);
    write_cntp_ctl(1);
    asm volatile("isb");
}

uint64_t systick_count(void) {
    return g_ticks;
}
