#pragma once

#include "common.h"

// Saved register state on a trap into EL1.
//
// IMPORTANT: the layout and total size (272 bytes) are mirrored by the
// kernel_entry / kernel_exit macros in src/arch/vectors.S. Keep them in sync.
struct trapframe {
    uint64_t regs[31];  // x0 .. x30                (offsets 0  .. 240)
    uint64_t sp;        // SP_EL0 (user stack)      (offset  248)
    uint64_t elr;       // ELR_EL1 (return address) (offset  256)
    uint64_t spsr;      // SPSR_EL1 (saved status)  (offset  264)
};

#define TRAPFRAME_SIZE 272
