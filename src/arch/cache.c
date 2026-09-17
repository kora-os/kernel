// SPDX-License-Identifier: GPL-3.0-or-later
//
// Data-cache maintenance by VA to the point of coherency. See arch/cache.h.

#include "arch/cache.h"

typedef enum { OP_CLEAN, OP_INVALIDATE, OP_CLEAN_INVALIDATE } dcache_op_t;

size_t dcache_line_size(void) {
    uint64_t ctr;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    // CTR_EL0.DminLine [19:16] = log2 of the smallest data cache line in words
    // (a word is 4 bytes).
    uint32_t dmin = (uint32_t)((ctr >> 16) & 0xF);
    return (size_t)(4u << dmin);
}

static void dcache_range(uintptr_t start, size_t size, dcache_op_t op) {
    if (size == 0) {
        return;
    }
    size_t line = dcache_line_size();
    uintptr_t addr = start & ~(uintptr_t)(line - 1);
    uintptr_t end = start + size;
    for (; addr < end; addr += line) {
        switch (op) {
        case OP_CLEAN:
            asm volatile("dc cvac, %0" ::"r"(addr) : "memory");
            break;
        case OP_INVALIDATE:
            asm volatile("dc ivac, %0" ::"r"(addr) : "memory");
            break;
        case OP_CLEAN_INVALIDATE:
            asm volatile("dc civac, %0" ::"r"(addr) : "memory");
            break;
        }
    }
    // Ensure the maintenance completes and is observed before/after device access.
    asm volatile("dsb sy" ::: "memory");
}

void dcache_clean(const void *addr, size_t size) {
    dcache_range((uintptr_t)addr, size, OP_CLEAN);
}

void dcache_invalidate(void *addr, size_t size) {
    dcache_range((uintptr_t)addr, size, OP_INVALIDATE);
}

void dcache_clean_invalidate(const void *addr, size_t size) {
    dcache_range((uintptr_t)addr, size, OP_CLEAN_INVALIDATE);
}
