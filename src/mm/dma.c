// SPDX-License-Identifier: GPL-3.0-or-later
//
// DMA buffer allocation and CPU/device cache synchronisation. See mm/dma.h.

#include "mm/dma.h"

#include "arch/cache.h"
#include "mm.h"
#include "mm/frame_alloc.h"

static size_t pages_for(size_t size) {
    return (size + PAGE_SIZE - 1) / PAGE_SIZE;
}

void *dma_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    return frame_alloc_pages(pages_for(size));
}

void dma_free(void *ptr, size_t size) {
    if (ptr == NULL || size == 0) {
        return;
    }
    frame_free_pages(ptr, pages_for(size));
}

void dma_sync_for_device(const void *ptr, size_t size) {
    dcache_clean(ptr, size);
}

void dma_sync_for_cpu(void *ptr, size_t size) {
    dcache_invalidate(ptr, size);
}
