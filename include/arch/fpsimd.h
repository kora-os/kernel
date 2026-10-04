// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Lazy FP/SIMD context switching.
//
// The kernel, Circle included, is integer-only (-mgeneral-regs-only), so the
// FP/SIMD registers only ever hold EL0 task state. They are not saved on a
// context switch. Instead CPACR_EL1 traps FP/SIMD use at EL0 and EL1 whenever
// the running task is not the one whose values are in the registers (the
// "owner"). The first FP instruction after a switch traps; the handler saves
// the previous owner's registers into its task, loads the current task's (or
// zeroes them on its first use) and makes it the owner. A task that never uses
// FP never pays for it. While the running task does not own the registers,
// EL1 FP use traps too and is reported as a kernel fault, which catches
// integer-only regressions at run time.

struct fpsimd_state {
    uint64_t v[64];      // q0..q31, 16 bytes each   (offset 0)
    uint32_t fpsr;       //                          (offset 512)
    uint32_t fpcr;       //                          (offset 516)
    uint64_t pad;
} __attribute__((aligned(16)));

struct task;

// After switching to `next`: allow FP/SIMD access only if `next` owns the
// registers.
void fpsimd_switch_to(struct task *next);

// EL0 FP/SIMD access trap (ESR EC 0x07): make the current task the owner.
void fpsimd_trap(void);

// A task is going away: forget it as the owner.
void fpsimd_release(struct task *t);

// --- implemented in src/arch/fpsimd.S ---
void fpsimd_save(struct fpsimd_state *st);
void fpsimd_load(const struct fpsimd_state *st);
void fpsimd_zero(void);
