// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// DMA buffer helpers for bus-mastering peripherals (USB, and later others).
//
// Buffers come from the frame allocator, so they are zeroed, page-aligned, and
// physically contiguous; under the flat identity map their virtual address is
// also their physical address. Note that the VideoCore/legacy DMA on the
// Broadcom SoCs sees a *bus* address (an alias of physical RAM), not the ARM
// physical address; that translation is peripheral-specific and is handled where
// each controller programs its descriptors (the vendored Circle USB stack does
// its own), not here.
//
// The sync helpers wrap the cache maintenance in arch/cache.h around the point
// where buffer ownership passes between CPU and device.

#ifdef __cplusplus
extern "C" {
#endif

// Allocate a zeroed, page-aligned, physically contiguous DMA buffer of at least
// `size` bytes. Returns NULL on failure.
void *dma_alloc(size_t size);

// Free a buffer from dma_alloc(). Pass the same size used to allocate.
void dma_free(void *ptr, size_t size);

// CPU has finished writing; make the data visible to the device (clean).
void dma_sync_for_device(const void *ptr, size_t size);

// Device has finished writing; discard stale CPU copies before reading
// (invalidate).
void dma_sync_for_cpu(void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
