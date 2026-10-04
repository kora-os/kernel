// SPDX-License-Identifier: GPL-3.0-or-later
//
// FP/SIMD register file save/restore for lazy context switching (see
// arch/fpsimd.h). These are the only kernel routines allowed to touch the
// FP/SIMD registers; tests/check-kernel-fp.py enforces it. The caller enables
// FP access in CPACR_EL1 first.

.arch armv8-a+fp+simd

.section .text

// void fpsimd_save(struct fpsimd_state *st /*x0*/)
.global fpsimd_save
fpsimd_save:
    stp  q0,  q1,  [x0, #32 * 0]
    stp  q2,  q3,  [x0, #32 * 1]
    stp  q4,  q5,  [x0, #32 * 2]
    stp  q6,  q7,  [x0, #32 * 3]
    stp  q8,  q9,  [x0, #32 * 4]
    stp  q10, q11, [x0, #32 * 5]
    stp  q12, q13, [x0, #32 * 6]
    stp  q14, q15, [x0, #32 * 7]
    stp  q16, q17, [x0, #32 * 8]
    stp  q18, q19, [x0, #32 * 9]
    stp  q20, q21, [x0, #32 * 10]
    stp  q22, q23, [x0, #32 * 11]
    stp  q24, q25, [x0, #32 * 12]
    stp  q26, q27, [x0, #32 * 13]
    stp  q28, q29, [x0, #32 * 14]
    stp  q30, q31, [x0, #32 * 15]
    mrs  x1, fpsr
    mrs  x2, fpcr
    add  x0, x0, #512
    stp  w1, w2, [x0]
    ret

// void fpsimd_load(const struct fpsimd_state *st /*x0*/)
.global fpsimd_load
fpsimd_load:
    ldp  q0,  q1,  [x0, #32 * 0]
    ldp  q2,  q3,  [x0, #32 * 1]
    ldp  q4,  q5,  [x0, #32 * 2]
    ldp  q6,  q7,  [x0, #32 * 3]
    ldp  q8,  q9,  [x0, #32 * 4]
    ldp  q10, q11, [x0, #32 * 5]
    ldp  q12, q13, [x0, #32 * 6]
    ldp  q14, q15, [x0, #32 * 7]
    ldp  q16, q17, [x0, #32 * 8]
    ldp  q18, q19, [x0, #32 * 9]
    ldp  q20, q21, [x0, #32 * 10]
    ldp  q22, q23, [x0, #32 * 11]
    ldp  q24, q25, [x0, #32 * 12]
    ldp  q26, q27, [x0, #32 * 13]
    ldp  q28, q29, [x0, #32 * 14]
    ldp  q30, q31, [x0, #32 * 15]
    add  x0, x0, #512
    ldp  w1, w2, [x0]
    msr  fpsr, x1
    msr  fpcr, x2
    ret

// void fpsimd_zero(void): the state a task sees on its first FP instruction.
.global fpsimd_zero
fpsimd_zero:
    movi v0.2d, #0
    movi v1.2d, #0
    movi v2.2d, #0
    movi v3.2d, #0
    movi v4.2d, #0
    movi v5.2d, #0
    movi v6.2d, #0
    movi v7.2d, #0
    movi v8.2d, #0
    movi v9.2d, #0
    movi v10.2d, #0
    movi v11.2d, #0
    movi v12.2d, #0
    movi v13.2d, #0
    movi v14.2d, #0
    movi v15.2d, #0
    movi v16.2d, #0
    movi v17.2d, #0
    movi v18.2d, #0
    movi v19.2d, #0
    movi v20.2d, #0
    movi v21.2d, #0
    movi v22.2d, #0
    movi v23.2d, #0
    movi v24.2d, #0
    movi v25.2d, #0
    movi v26.2d, #0
    movi v27.2d, #0
    movi v28.2d, #0
    movi v29.2d, #0
    movi v30.2d, #0
    movi v31.2d, #0
    msr  fpsr, xzr
    msr  fpcr, xzr
    ret
