#pragma once

// Real ARMv8-A (AArch64) system-register bit definitions for KoraOS.
//
// Only the fields KoraOS actually programs during bring-up are defined here:
// the EL2/EL3 -> EL1 handoff, exception handling, and the flat identity MMU.
// Values are taken from the ARM Architecture Reference Manual (ARMv8-A).

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

// -------------------------------------------------------------------------
// SCTLR_EL1 - System Control Register (EL1)
// -------------------------------------------------------------------------
#define SCTLR_RESERVED          (BIT(29) | BIT(28) | BIT(23) | BIT(22) | BIT(20) | BIT(11))
#define SCTLR_EE_LITTLE_ENDIAN  (0 << 25)
#define SCTLR_EOE_LITTLE_ENDIAN (0 << 24)
#define SCTLR_I_CACHE           BIT(12)  // instruction cache enable
#define SCTLR_D_CACHE           BIT(2)   // data cache enable
#define SCTLR_MMU_ENABLED       BIT(0)   // stage-1 MMU enable

#define SCTLR_MMU_DISABLED      0

#define SCTLR_VALUE_MMU_DISABLED \
    (SCTLR_RESERVED | SCTLR_EE_LITTLE_ENDIAN | SCTLR_EOE_LITTLE_ENDIAN | SCTLR_MMU_DISABLED)

// -------------------------------------------------------------------------
// HCR_EL2 - Hypervisor Configuration Register
// -------------------------------------------------------------------------
#define HCR_RW       BIT(31)  // EL1 is AArch64
#define HCR_VALUE    HCR_RW

// -------------------------------------------------------------------------
// SCR_EL3 - Secure Configuration Register
// -------------------------------------------------------------------------
#define SCR_RESERVED (3 << 4)
#define SCR_RW       BIT(10)  // next lower EL is AArch64
#define SCR_NS       BIT(0)   // non-secure
#define SCR_VALUE    (SCR_RESERVED | SCR_RW | SCR_NS)

// -------------------------------------------------------------------------
// SPSR_ELx - Saved Program Status (used as eret target state)
// -------------------------------------------------------------------------
#define SPSR_MASK_ALL (7 << 6)   // mask D, A, I, F
#define SPSR_EL1h     (5 << 0)   // return to EL1, using SP_EL1
#define SPSR_EL2h     (9 << 0)   // return to EL2, using SP_EL2
#define SPSR_VALUE_EL1 (SPSR_MASK_ALL | SPSR_EL1h)
#define SPSR_VALUE_EL2 (SPSR_MASK_ALL | SPSR_EL2h)

// EL0t: return to EL0 (necessarily SP_EL0), all interrupts unmasked.
#define SPSR_EL0t      (0)

// -------------------------------------------------------------------------
// ESR_EL1 - Exception Syndrome Register
// -------------------------------------------------------------------------
#define ESR_EC_SHIFT   26
#define ESR_EC_MASK    0x3F
#define ESR_ISS_MASK   0xFFFFFF
#define ESR_EC(esr)    (((esr) >> ESR_EC_SHIFT) & ESR_EC_MASK)

// Exception classes we care about during bring-up.
#define ESR_EC_SVC64        0x15  // SVC instruction execution in AArch64
#define ESR_EC_DABT_LOWER   0x24  // data abort from a lower EL
#define ESR_EC_DABT_SAME    0x25  // data abort from current EL
#define ESR_EC_IABT_LOWER   0x20  // instruction abort from a lower EL
#define ESR_EC_IABT_SAME    0x21  // instruction abort from current EL

// -------------------------------------------------------------------------
// MAIR_EL1 - Memory Attribute Indirection Register
// -------------------------------------------------------------------------
// attr index 0: Normal memory, inner/outer write-back non-transient
// attr index 1: Device-nGnRnE
#define MAIR_DEVICE_nGnRnE     0x00
#define MAIR_NORMAL_NC         0x44
#define MAIR_NORMAL_WB         0xFF

#define MAIR_IDX_NORMAL        0
#define MAIR_IDX_DEVICE        1

#define MAIR_VALUE \
    ((MAIR_NORMAL_WB    << (8 * MAIR_IDX_NORMAL)) | \
     (MAIR_DEVICE_nGnRnE << (8 * MAIR_IDX_DEVICE)))

// -------------------------------------------------------------------------
// TCR_EL1 - Translation Control Register
// -------------------------------------------------------------------------
// 48-bit VA (T0SZ = 16), 4KB granule on TTBR0, inner-shareable WB cacheable.
#define TCR_T0SZ        (64 - 48)
#define TCR_TG0_4K      (0 << 14)
#define TCR_SH0_INNER   (3 << 12)
#define TCR_ORGN0_WB    (1 << 10)
#define TCR_IRGN0_WB    (1 << 8)
// IPS (bits 32..34) is OR'd in at runtime from ID_AA64MMFR0_EL1.

#define TCR_VALUE \
    (TCR_T0SZ | TCR_TG0_4K | TCR_SH0_INNER | TCR_ORGN0_WB | TCR_IRGN0_WB)

// -------------------------------------------------------------------------
// Stage-1 page/block descriptor attributes (flat permissive map)
// -------------------------------------------------------------------------
#define PD_TABLE        0x3   // table descriptor (next level)
#define PD_BLOCK        0x1   // block descriptor (2MB at L2)
#define PD_VALID        0x1

#define PD_ATTR_IDX(i)  ((i) << 2)     // AttrIndx -> MAIR index
#define PD_NS           BIT(5)
#define PD_AP_RW_ALL    (1 << 6)       // AP[2:1]=01: EL1+EL0 read/write
#define PD_SH_INNER     (3 << 8)       // inner shareable
#define PD_AF           BIT(10)        // access flag (set so no fault on first access)
// UXN/PXN deliberately left clear -> execute permitted from EL0 and EL1.

// Normal-memory 2MB block, RWX, accessible from EL0 and EL1.
#define MMU_NORMAL_BLOCK_FLAGS \
    (PD_BLOCK | PD_AF | PD_SH_INNER | PD_AP_RW_ALL | PD_ATTR_IDX(MAIR_IDX_NORMAL))

// Device 2MB block, RW, accessible from EL0 and EL1 (outer-shareable implied by device).
#define MMU_DEVICE_BLOCK_FLAGS \
    (PD_BLOCK | PD_AF | PD_AP_RW_ALL | PD_ATTR_IDX(MAIR_IDX_DEVICE))
