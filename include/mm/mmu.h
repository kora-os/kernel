#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Build a flat, fully-permissive identity map of the low 4 GB and enable the
// MMU at EL1. Every 2 MB block is mapped read/write/execute for both EL1 and
// EL0 (no memory protection by design); regions at or above the SoC
// peripheral base (PBASE) are mapped as Device memory, the rest as Normal
// write-back cacheable memory.
//
// After this returns the kernel runs with caches and the MMU enabled. The UART
// must still work, which is the primary correctness check.
void mmu_init(void);

// Remap the 2 MB blocks covering [base, base + size) as Normal non-cacheable,
// for memory a device reads or writes behind the ARM's caches (e.g. the
// framebuffer the display scans out). Whole blocks change, so any neighbouring
// data in them becomes uncached too.
void mmu_map_coherent(uintptr_t base, size_t size);

// Make page 0 (firmware data: the ARM stub's spin table and, at 0xF8, the
// device-tree pointer) writable, or read-only again. It is read-only by default
// so that NULL-pointer writes fault; Circle's CMachineInfo clears the
// device-tree pointer after reading it, so page 0 is opened just for that.
void mmu_set_firmware_page_writable(int writable);

#ifdef __cplusplus
}
#endif
