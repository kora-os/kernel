// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Per-CPU data. Each core's TPIDR_EL1 points at its own struct cpu, so
// this_cpu() is a single register read and needs no lock: only the owning
// core touches its own entry (other cores may read it for diagnostics).

#define MAX_CPUS 4

struct task;

struct cpu {
    unsigned id;
    struct task *curr;        // the task running on this core
    struct task *idle;        // its boot thread (task 0 on core 0)
    volatile bool need_resched;  // switch tasks on the next return to EL0
    struct task *fp_owner;    // whose state is in this core's FP/SIMD registers
    int locks_held;           // spinlocks this core holds (debug checks)
    bool panicking;           // panic(): bypass locks so the message gets out
};

extern struct cpu cpus[MAX_CPUS];

// Point this core's TPIDR_EL1 at cpus[id]. Call first thing on each core.
void percpu_init(unsigned id);

#ifdef KORAOS_HOST_TEST
struct cpu *this_cpu(void);  // provided by the host test
#else
static inline struct cpu *this_cpu(void) {
    struct cpu *c;
    asm volatile("mrs %0, tpidr_el1" : "=r"(c));
    return c;
}
#endif
