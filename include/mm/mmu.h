#pragma once

// Build a flat, fully-permissive identity map of the low 4 GB and enable the
// MMU at EL1. Every 2 MB block is mapped read/write/execute for both EL1 and
// EL0 (no memory protection by design); regions at or above the SoC
// peripheral base (PBASE) are mapped as Device memory, the rest as Normal
// write-back cacheable memory.
//
// After this returns the kernel runs with caches and the MMU enabled. The UART
// must still work, which is the primary correctness check.
void mmu_init(void);
