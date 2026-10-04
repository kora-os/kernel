// SPDX-License-Identifier: GPL-3.0-or-later
//
// Periodic system tick on the generic timer. See arch/systick.h.

#include "arch/systick.h"

#include "arch/irq.h"
#include "lib/timer.h"
#include "peripherals/irq.h"
#include "proc/task.h"

static volatile uint64_t g_ticks;
static unsigned g_hz;
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
    sched_tick(g_ticks);
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
    g_hz = hz;
    g_interval = timer_freq_hz() / hz;
    g_ticks = 0;

    // Let EL0 read the virtual counter (CNTKCTL_EL1.EL0VCTEN), so programs
    // can time themselves without a syscall; CNTFRQ_EL0 is always readable.
    uint64_t kctl;
    asm volatile("mrs %0, cntkctl_el1" : "=r"(kctl));
    asm volatile("msr cntkctl_el1, %0" ::"r"(kctl | (1u << 1)));

    // Enabling the IRQ also routes the timer event to this core (the local
    // controller on the Pi 3, a per-core PPI in the GIC on the Pi 4).
    irq_connect(IRQ_TIMER_CNTPNS, systick_isr, NULL);

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

unsigned systick_hz(void) {
    return g_hz;
}
