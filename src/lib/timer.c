// SPDX-License-Identifier: GPL-3.0-or-later
//
// Wall-clock delays on the ARMv8 generic timer. See include/lib/timer.h.

#include "lib/timer.h"

static inline uint64_t read_cntfrq(void) {
    uint64_t v;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t read_cntpct(void) {
    uint64_t v;
    // isb so the counter read is not speculated ahead of prior instructions;
    // this matters when timing tight device-reset windows.
    asm volatile("isb; mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

uint64_t timer_freq_hz(void) {
    return read_cntfrq();
}

uint64_t timer_ticks(void) {
    return read_cntpct();
}

uint64_t timer_us(void) {
    uint64_t freq = read_cntfrq();
    uint64_t ticks = read_cntpct();
    // Split whole/remainder so we neither lose precision nor overflow a 64-bit
    // multiply for large uptimes.
    return (ticks / freq) * 1000000ULL + ((ticks % freq) * 1000000ULL) / freq;
}

void udelay(uint64_t us) {
    uint64_t freq = read_cntfrq();
    uint64_t start = read_cntpct();
    // For any sane delay (us well under ~10^11) this multiply stays in range.
    uint64_t target = (us * freq) / 1000000ULL;
    while ((read_cntpct() - start) < target) {
        asm volatile("nop");
    }
}

void mdelay(uint64_t ms) {
    udelay(ms * 1000ULL);
}
