// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kernel context switch for KoraOS.
//
// cpu_switch() saves the callee-saved registers, the frame pointer, the return
// address and the stack pointer into the outgoing task's struct cpu_context
// (include/proc/task.h) and loads the incoming task's, then returns on the
// incoming task's kernel stack. Everything else a C caller may need is
// caller-saved, so this is the whole kernel context. A new task's context
// returns into ret_to_user (src/arch/vectors.S) with sp at its trap frame.
//
// struct cpu_context is 13 * 8 = 104 bytes: x19..x28, x29, x30, sp.

.section .text

.global cpu_switch
// void cpu_switch(struct cpu_context *from /*x0*/, struct cpu_context *to /*x1*/)
cpu_switch:
    stp  x19, x20, [x0, #0]
    stp  x21, x22, [x0, #16]
    stp  x23, x24, [x0, #32]
    stp  x25, x26, [x0, #48]
    stp  x27, x28, [x0, #64]
    stp  x29, x30, [x0, #80]
    mov  x9, sp
    str  x9,       [x0, #96]

    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #80]
    ldr  x9,       [x1, #96]
    mov  sp, x9
    ret
