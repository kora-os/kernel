#include "arch/sysregs.h"

.global _start

.section .text

.equ CORE_ID_MASK,     0xFF

_start:
    // Park secondary cores
    mrs x1, MPIDR_EL1
    and x1, x1, #CORE_ID_MASK
    cbz x1, primary_core
    b CORE_PARK

primary_core:
    // Drop to EL1 if we booted in a higher exception level, then continue
    // at el1_entry. KoraOS runs its kernel in EL1.
    bl switch_to_el1

el1_entry:
    // Set up stack pointer (SP_EL1)
    ldr x0, =stack_top
    mov sp, x0

    // Clear BSS
    ldr x10, =__bss_start
    ldr x11, =__bss_end
clear_bss:
    cmp x10, x11
    b.hs bss_cleared
    str xzr, [x10], #8
    b clear_bss

bss_cleared:
    // Jump to kernel main
    bl kernel_main
    // If kernel_main returns, hang
    b hang

// switch_to_el1: configure the appropriate higher EL and eret down to EL1,
// landing at el1_entry. If already in EL1, branch there directly. Uses only
// scratch registers (no stack), since SP is not set up until el1_entry.
switch_to_el1:
    mrs x0, CurrentEL
    lsr x0, x0, #2          // x0 = current EL (1, 2 or 3)

    cmp x0, #3
    bne check_el2

    // --- From EL3: route directly down to EL1 ---
    ldr x1, =SCR_VALUE
    msr scr_el3, x1
    ldr x1, =HCR_VALUE          // EL1 is AArch64
    msr hcr_el2, x1
    ldr x1, =SCTLR_VALUE_MMU_DISABLED
    msr sctlr_el1, x1
    ldr x1, =SPSR_VALUE_EL1     // return to EL1h, all interrupts masked
    msr spsr_el3, x1
    adr x1, el1_entry
    msr elr_el3, x1
    eret

check_el2:
    cmp x0, #2
    bne already_el1

    // --- From EL2: drop to EL1 ---
    ldr x1, =HCR_VALUE          // EL1 is AArch64
    msr hcr_el2, x1
    ldr x1, =SCTLR_VALUE_MMU_DISABLED
    msr sctlr_el1, x1
    // Let EL1 use the physical/virtual timers without trapping.
    mrs x1, cnthctl_el2
    orr x1, x1, #3
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr
    ldr x1, =SPSR_VALUE_EL1     // return to EL1h, all interrupts masked
    msr spsr_el2, x1
    adr x1, el1_entry
    msr elr_el2, x1
    eret

already_el1:
    b el1_entry

CORE_PARK:
    mov x9, x1
SECONDARY_WAIT:
    wfi
    b SECONDARY_WAIT

hang:
    wfi
    b hang
