// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-CPU data. See arch/percpu.h.

#include "arch/percpu.h"

struct cpu cpus[MAX_CPUS];

void percpu_init(unsigned id) {
    struct cpu *c = &cpus[id];
    c->id = id;
    asm volatile("msr tpidr_el1, %0" ::"r"(c) : "memory");
}
