// EL0 entry/exit for KoraOS' single-task model.
//
// enter_user() saves the kernel's callee-saved register context (a setjmp-like
// snapshot) and erets down to EL0. kernel_return() restores that snapshot so
// the exit syscall can unwind out of EL0 straight back to enter_user()'s
// caller, abandoning the trap frame on the kernel stack.

.section .bss
.align 4
kernel_ctx:
    .skip 104               // x19..x30 (12) + sp = 13 * 8

.section .text

.global enter_user
enter_user:
    // x0 = task_t* ; task_t layout: entry @0, user_sp @8
    adrp x1, kernel_ctx
    add  x1, x1, :lo12:kernel_ctx
    mov  x2, sp
    stp  x19, x20, [x1, #0]
    stp  x21, x22, [x1, #16]
    stp  x23, x24, [x1, #32]
    stp  x25, x26, [x1, #48]
    stp  x27, x28, [x1, #64]
    stp  x29, x30, [x1, #80]
    str  x2,       [x1, #96]

    ldr  x2, [x0, #0]       // entry
    ldr  x3, [x0, #8]       // user_sp
    msr  elr_el1, x2        // return address for eret
    msr  sp_el0,  x3        // user stack
    msr  spsr_el1, xzr      // EL0t, all interrupts unmasked
    mov  x0, #0
    isb
    eret

.global kernel_return
kernel_return:
    adrp x1, kernel_ctx
    add  x1, x1, :lo12:kernel_ctx
    ldp  x19, x20, [x1, #0]
    ldp  x21, x22, [x1, #16]
    ldp  x23, x24, [x1, #32]
    ldp  x25, x26, [x1, #48]
    ldp  x27, x28, [x1, #64]
    ldp  x29, x30, [x1, #80]
    ldr  x2,       [x1, #96]
    mov  sp, x2
    ret                     // return into enter_user()'s caller (task_run)
