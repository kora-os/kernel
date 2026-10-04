// SPDX-License-Identifier: GPL-3.0-or-later
//
// Lazy FP/SIMD ownership. See arch/fpsimd.h.

#include "arch/fpsimd.h"

#include "proc/task.h"

#define CPACR_FPEN_SHIFT 20
#define CPACR_FPEN_MASK (3ul << CPACR_FPEN_SHIFT)
#define CPACR_FPEN_NONE (0ul << CPACR_FPEN_SHIFT)  // trap EL0 and EL1
#define CPACR_FPEN_ALL (3ul << CPACR_FPEN_SHIFT)   // trap nothing

// The task whose values are in the FP/SIMD registers, or NULL. One per core
// once secondary cores run tasks (tasks never migrate).
static task_t *fp_owner;

static void set_access(bool allowed) {
    uint64_t cpacr;
    asm volatile("mrs %0, cpacr_el1" : "=r"(cpacr));
    cpacr = (cpacr & ~CPACR_FPEN_MASK) | (allowed ? CPACR_FPEN_ALL : CPACR_FPEN_NONE);
    asm volatile("msr cpacr_el1, %0\n\tisb" ::"r"(cpacr) : "memory");
}

void fpsimd_switch_to(task_t *next) {
    set_access(next != NULL && next == fp_owner);
}

void fpsimd_trap(void) {
    task_t *t = task_running();
    set_access(true);
    if (fp_owner != t) {
        if (fp_owner != NULL) {
            fpsimd_save(&fp_owner->fp);
        }
        if (t->fp_used) {
            fpsimd_load(&t->fp);
        } else {
            fpsimd_zero();
            t->fp_used = true;
        }
        fp_owner = t;
    }
}

void fpsimd_release(task_t *t) {
    if (fp_owner == t) {
        fp_owner = NULL;
        set_access(false);
    }
}
