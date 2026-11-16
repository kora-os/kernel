// SPDX-License-Identifier: GPL-3.0-or-later
//
// setjmp and longjmp (C11 7.13) for AArch64. The buffer holds what a callee
// must preserve under AAPCS64: x19 to x28, the frame pointer x29, the link
// register x30, the stack pointer and the low halves of v8 to v15.
//
//   offset   0 ..  95   x19 .. x30
//   offset  96          sp
//   offset 104 .. 167   d8 .. d15
//
// The FP registers make a program that calls setjmp an FP user for the
// kernel's lazy FP switching, which every program printing through stdio
// already is.
//
// The symbol macros let the same file assemble for an arm64 host (Mach-O
// prefixes symbols with an underscore), where the host tests run it.

#define SYM2(prefix, name) prefix##name
#define SYM1(prefix, name) SYM2(prefix, name)
#define SYM(name) SYM1(__USER_LABEL_PREFIX__, name)

    .text

// int setjmp(jmp_buf env): saves the caller's context, returns 0.
    .p2align 2
    .global SYM(setjmp)
#ifdef __ELF__
    .type SYM(setjmp), %function
#endif
SYM(setjmp):
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]
    mov  x2, sp
    str  x2, [x0, #96]
    stp  d8, d9, [x0, #104]
    stp  d10, d11, [x0, #120]
    stp  d12, d13, [x0, #136]
    stp  d14, d15, [x0, #152]
    mov  w0, #0
    ret
#ifdef __ELF__
    .size SYM(setjmp), . - SYM(setjmp)
#endif

// void longjmp(jmp_buf env, int val): returns from the setjmp that filled
// env, with val, or with 1 if val is 0.
    .p2align 2
    .global SYM(longjmp)
#ifdef __ELF__
    .type SYM(longjmp), %function
#endif
SYM(longjmp):
    ldp  x19, x20, [x0, #0]
    ldp  x21, x22, [x0, #16]
    ldp  x23, x24, [x0, #32]
    ldp  x25, x26, [x0, #48]
    ldp  x27, x28, [x0, #64]
    ldp  x29, x30, [x0, #80]
    ldr  x2, [x0, #96]
    mov  sp, x2
    ldp  d8, d9, [x0, #104]
    ldp  d10, d11, [x0, #120]
    ldp  d12, d13, [x0, #136]
    ldp  d14, d15, [x0, #152]
    cmp  w1, #0
    csinc w0, w1, wzr, ne     // val, or 1 for a val of 0
    ret                       // to the saved x30: just after the setjmp call
#ifdef __ELF__
    .size SYM(longjmp), . - SYM(longjmp)
#endif
