// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lazy FP/SIMD ownership. See arch/fpsimd.h.

#include "arch/fpsimd.h"

#include "arch/percpu.h"
#include "proc/task.h"

#define CPACR_FPEN_SHIFT 20
#define CPACR_FPEN_MASK (3ul << CPACR_FPEN_SHIFT)
#define CPACR_FPEN_NONE (0ul << CPACR_FPEN_SHIFT)  // trap EL0 and EL1
#define CPACR_FPEN_ALL (3ul << CPACR_FPEN_SHIFT)   // trap nothing

// The task whose values are in this core's FP/SIMD registers is
// this_cpu()->fp_owner (tasks never migrate between cores). Only the owning
// core touches it, with IRQs masked (traps and the scheduler), so no lock.

static void set_access(bool allowed) {
    uint64_t cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr = (cpacr & ~CPACR_FPEN_MASK) | (allowed ? CPACR_FPEN_ALL : CPACR_FPEN_NONE);
    asm volatile("msr cpacr_el1, %0\n\tisb" ::"r"(cpacr) : "memory");
}

void fpsimd_switch_to(task_t *next) {
    set_access(next != NULL && next == this_cpu()->fp_owner);
}

void fpsimd_trap(void) {
    struct cpu *c = this_cpu();
    task_t *t = c->curr;
    set_access(true);
    if (c->fp_owner != t) {
        if (c->fp_owner != NULL) {
            fpsimd_save(&c->fp_owner->fp);
        }
        if (t->fp_used) {
            fpsimd_load(&t->fp);
        } else {
            fpsimd_zero();
            t->fp_used = true;
        }
        c->fp_owner = t;
    }
}

void fpsimd_release(task_t *t) {
    struct cpu *c = this_cpu();
    if (c->fp_owner == t) {
        c->fp_owner = NULL;
        set_access(false);
    }
}
