// EL0 entry/exit for KoraOS.
//
// enter_user() saves the kernel's callee-saved register context (a setjmp-like
// snapshot) into a caller-provided buffer and erets down to EL0. Because the
// buffer belongs to the task rather than being a single global, spawns can
// nest: a parent suspended inside its spawn syscall keeps its own snapshot
// while the child runs. kernel_return() restores a given snapshot, unwinding
// out of EL0 straight back to the matching enter_user() caller and abandoning
// the trap frame on the kernel stack.
//
// The buffer is 13 * 8 = 104 bytes: x19..x30 (12) followed by sp.

.section .text

.global enter_user
// void enter_user(uint64_t entry /*x0*/, uint64_t user_sp /*x1*/,
//                 uint64_t *kctx /*x2*/)
enter_user:
    stp  x19, x20, [x2, #0]
    stp  x21, x22, [x2, #16]
    stp  x23, x24, [x2, #32]
    stp  x25, x26, [x2, #48]
    stp  x27, x28, [x2, #64]
    stp  x29, x30, [x2, #80]
    mov  x3, sp
    str  x3,       [x2, #96]

    msr  elr_el1, x0           // entry -> eret return address
    msr  sp_el0,  x1           // user stack top
    msr  spsr_el1, xzr         // EL0t, all interrupts unmasked
    mov  x0, #0                // EL0 starts with x0 = 0
    isb
    eret

.global kernel_return
// void kernel_return(uint64_t *kctx /*x0*/)
kernel_return:
    ldp  x19, x20, [x0, #0]
    ldp  x21, x22, [x0, #16]
    ldp  x23, x24, [x0, #32]
    ldp  x25, x26, [x0, #48]
    ldp  x27, x28, [x0, #64]
    ldp  x29, x30, [x0, #80]
    ldr  x2,       [x0, #96]
    mov  sp, x2
    ret                        // return into enter_user()'s caller
