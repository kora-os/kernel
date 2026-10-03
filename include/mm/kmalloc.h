// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

// Kernel heap: small, freeable kernel objects on top of the frame allocator.
//
// Requests up to KMALLOC_MAX_SMALL bytes come from size-class slabs (one 4 KB
// page each); larger ones fall through to whole page runs. Every block is at
// least KMALLOC_MIN_ALIGN aligned and padded to a multiple of it, so two blocks
// never share a cache line (DMA cache maintenance on one cannot corrupt
// another). Memory is returned zeroed. All entry points are safe to call from
// interrupt handlers (they mask IRQs around their bookkeeping).

#define KMALLOC_MIN_ALIGN 64
#define KMALLOC_MAX_SMALL 1344
#define KMALLOC_CLASSES 9

// Allocate `size` zeroed bytes, KMALLOC_MIN_ALIGN aligned. `size == 0` returns
// a unique minimum-size block. Returns NULL when memory is exhausted.
void *kmalloc(size_t size);

// As kmalloc(), aligned to `align` (a power of two; smaller values than
// KMALLOC_MIN_ALIGN are rounded up). Returns NULL for an invalid alignment.
void *kmalloc_aligned(size_t size, size_t align);

// Free a block from kmalloc()/kmalloc_aligned(). NULL is ignored. A pointer the
// heap did not hand out, or a block that is already free, is reported on the
// console, counted in kmalloc_stats.bad_frees and otherwise ignored.
void kfree(void *ptr);

struct kmalloc_class_stats {
    size_t block_size;  // bytes per block
    size_t slabs;       // pages currently owned by this class
    size_t used;        // blocks handed out
    size_t capacity;    // blocks in those pages
};

struct kmalloc_stats {
    struct kmalloc_class_stats classes[KMALLOC_CLASSES];
    size_t large_allocs;  // live page-run allocations
    size_t large_pages;   // pages they occupy
    size_t live_allocs;   // all live allocations, small and large
    size_t total_allocs;  // successful allocations since boot
    size_t failed_allocs; // requests that returned NULL
    size_t bad_frees;     // invalid or double frees caught by kfree()
};

void kmalloc_get_stats(struct kmalloc_stats *out);

// Deterministic allocation stress over kmalloc/kmalloc_aligned/kfree: random
// sizes, alignments and lifetimes, with every block's contents checked before
// it is freed. Frees everything it allocated. Returns 0 on success or a
// positive code naming the first check that failed.
int kmalloc_stress(unsigned rounds, uint32_t seed);

#ifdef __cplusplus
}
#endif
