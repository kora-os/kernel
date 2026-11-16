// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test helper for setjmp and longjmp on AArch64, used by the host test
// (tests/setjmp_test.c, on arm64 hosts) and on the target by libcprobe.
//
//   long setjmp_registers(jmp_buf env);
//
// Fills every register a callee must preserve (x19 to x28, d8 to d15) with a
// pattern, calls setjmp(env), overwrites them all with a second pattern and
// calls longjmp(env, 5). Back from setjmp for the second time, it compares
// each register with the first pattern. Returns 0 if all of them, the frame
// pointer, the stack pointer and the value 5 came back; otherwise a bit mask:
// bits 0 to 9 for x19 to x28, bits 10 to 17 for d8 to d15, bit 18 for the
// frame pointer, bit 19 for the stack pointer, bit 20 for the value.
// C cannot test this: the compiler decides what lives in which register.

#define SYM2(prefix, name) prefix##name
#define SYM1(prefix, name) SYM2(prefix, name)
#define SYM(name) SYM1(__USER_LABEL_PREFIX__, name)

#define FRAME 192   // x29, x30, env, sp copy, x19..x28 (10), d8..d15 (8)

// Set bit `bit` of the result mask in x10 unless the register holds `value`.
.macro check_x reg, value, bit
    mov  x9, #\value
    cmp  \reg, x9
    cset x9, ne
    orr  x10, x10, x9, lsl #\bit
.endm

.macro check_d reg, value, bit
    fmov x11, \reg
    check_x x11, \value, \bit
.endm

    .text
    .p2align 2
    .global SYM(setjmp_registers)
#ifdef __ELF__
    .type SYM(setjmp_registers), %function
#endif
SYM(setjmp_registers):
    stp  x29, x30, [sp, #-FRAME]!
    mov  x29, sp
    str  x0, [sp, #16]            // env
    stp  x19, x20, [sp, #32]      // our caller's registers
    stp  x21, x22, [sp, #48]
    stp  x23, x24, [sp, #64]
    stp  x25, x26, [sp, #80]
    stp  x27, x28, [sp, #96]
    stp  d8, d9, [sp, #112]
    stp  d10, d11, [sp, #128]
    stp  d12, d13, [sp, #144]
    stp  d14, d15, [sp, #160]

    // First pattern: x<n> = 0xa500 + n, d<n> = bits 0xd800 + n.
    mov  x19, #0xa513
    mov  x20, #0xa514
    mov  x21, #0xa515
    mov  x22, #0xa516
    mov  x23, #0xa517
    mov  x24, #0xa518
    mov  x25, #0xa519
    mov  x26, #0xa51a
    mov  x27, #0xa51b
    mov  x28, #0xa51c
    mov  x9, #0xd808
    fmov d8, x9
    add  x9, x9, #1
    fmov d9, x9
    add  x9, x9, #1
    fmov d10, x9
    add  x9, x9, #1
    fmov d11, x9
    add  x9, x9, #1
    fmov d12, x9
    add  x9, x9, #1
    fmov d13, x9
    add  x9, x9, #1
    fmov d14, x9
    add  x9, x9, #1
    fmov d15, x9

    ldr  x0, [sp, #16]
    bl   SYM(setjmp)
    cbnz w0, 1f

    // First return: wreck everything, then jump back.
    mov  x19, #0x1111
    mov  x20, x19
    mov  x21, x19
    mov  x22, x19
    mov  x23, x19
    mov  x24, x19
    mov  x25, x19
    mov  x26, x19
    mov  x27, x19
    mov  x28, x19
    fmov d8, x19
    fmov d9, x19
    fmov d10, x19
    fmov d11, x19
    fmov d12, x19
    fmov d13, x19
    fmov d14, x19
    fmov d15, x19
    ldr  x0, [sp, #16]
    sub  sp, sp, #64              // a deeper stack and another frame pointer
    mov  x29, #0x2222
    mov  w1, #5
    bl   SYM(longjmp)
    brk  #1                       // longjmp returned

    // Second return: w0 is longjmp's value.
1:  mov  x10, #0                  // result mask
    cmp  w0, #5
    b.eq 2f
    orr  x10, x10, #(1 << 20)
2:  mov  x11, sp
    cmp  x29, x11                 // the frame pointer was sp at the call
    b.eq 3f
    orr  x10, x10, #(1 << 18)
3:  ldr  x12, [x29, #16]          // through the frame pointer: env is intact
    ldr  x12, [x12, #96]          // the sp setjmp saved
    cmp  x11, x12
    b.eq 4f
    orr  x10, x10, #(1 << 19)
4:
    check_x x19, 0xa513, 0
    check_x x20, 0xa514, 1
    check_x x21, 0xa515, 2
    check_x x22, 0xa516, 3
    check_x x23, 0xa517, 4
    check_x x24, 0xa518, 5
    check_x x25, 0xa519, 6
    check_x x26, 0xa51a, 7
    check_x x27, 0xa51b, 8
    check_x x28, 0xa51c, 9
    check_d d8, 0xd808, 10
    check_d d9, 0xd809, 11
    check_d d10, 0xd80a, 12
    check_d d11, 0xd80b, 13
    check_d d12, 0xd80c, 14
    check_d d13, 0xd80d, 15
    check_d d14, 0xd80e, 16
    check_d d15, 0xd80f, 17

    mov  x0, x10
    ldp  x19, x20, [sp, #32]
    ldp  x21, x22, [sp, #48]
    ldp  x23, x24, [sp, #64]
    ldp  x25, x26, [sp, #80]
    ldp  x27, x28, [sp, #96]
    ldp  d8, d9, [sp, #112]
    ldp  d10, d11, [sp, #128]
    ldp  d12, d13, [sp, #144]
    ldp  d14, d15, [sp, #160]
    ldp  x29, x30, [sp], #FRAME
    ret
#ifdef __ELF__
    .size SYM(setjmp_registers), . - SYM(setjmp_registers)
#endif
