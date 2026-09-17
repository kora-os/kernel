// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Data-cache maintenance by virtual address. KoraOS runs with the D-cache on and
// RAM mapped Normal write-back cacheable (see src/mm/mmu.c), so any buffer shared
// with a bus-mastering device (USB, DMA) must be kept coherent by hand: clean
// before the device reads it, invalidate before the CPU reads what it wrote.
//
// For DMA buffers prefer the higher-level helpers in mm/dma.h, which pair these
// with page-aligned allocation. Ranges are rounded out to cache-line boundaries;
// size DMA buffers to whole cache lines so an invalidate never discards a
// neighbouring live value.

#ifdef __cplusplus
extern "C" {
#endif

// Data cache line size in bytes (from CTR_EL0).
size_t dcache_line_size(void);

// Clean (write back) dirty lines covering the range to the point of coherency.
void dcache_clean(const void *addr, size_t size);

// Invalidate lines covering the range (discard cached copies).
void dcache_invalidate(void *addr, size_t size);

// Clean then invalidate lines covering the range.
void dcache_clean_invalidate(const void *addr, size_t size);

#ifdef __cplusplus
}
#endif
