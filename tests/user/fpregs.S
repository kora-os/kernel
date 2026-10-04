// SPDX-License-Identifier: GPL-3.0-or-later
//
// FP/SIMD register helpers for fpprobe. User programs are built
// -mgeneral-regs-only, so the C code never touches these registers and they
// hold exactly what these routines put there across syscalls.

.arch armv8-a+fp+simd

.section .text

#define FPCR_PROBE 0x00c00000   // rounding mode: towards zero

// void fp_fill(unsigned long seed): v<i> = {seed + i, seed + i}, FPCR = probe.
.global fp_fill
fp_fill:
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    add  x1, x0, #\i
    dup  v\i\().2d, x1
.endr
    mov  x1, #FPCR_PROBE
    msr  fpcr, x1
    ret

// unsigned long fp_check(unsigned long seed): 0 if fp_fill(seed) state is intact.
.global fp_check
fp_check:
    mov  x4, #0
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    add  x1, x0, #\i
    mov  x2, v\i\().d[0]
    mov  x3, v\i\().d[1]
    eor  x2, x2, x1
    eor  x3, x3, x1
    orr  x4, x4, x2
    orr  x4, x4, x3
.endr
    mrs  x2, fpcr
    mov  x1, #FPCR_PROBE
    eor  x2, x2, x1
    orr  x0, x4, x2
    ret

// unsigned long fp_nonzero(void): 0 if every FP/SIMD register and FPCR is zero.
.global fp_nonzero
fp_nonzero:
    mov  x4, #0
.irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    mov  x2, v\i\().d[0]
    mov  x3, v\i\().d[1]
    orr  x4, x4, x2
    orr  x4, x4, x3
.endr
    mrs  x2, fpcr
    orr  x0, x4, x2
    ret
