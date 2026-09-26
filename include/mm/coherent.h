// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Coherent (Normal non-cacheable) memory for buffers shared with a bus master
// that does not participate in cache coherency, such as the VideoCore mailbox.
// Backed by a fixed 2 MB region mapped non-cacheable by the MMU (src/mm/mmu.c).
// USB transfer buffers do NOT use this: the DWC2 driver does its own cache
// maintenance on ordinary cached memory.

// Slot for KoraOS's own property-mailbox buffer (video/framebuffer.c). Circle
// reserves slots 0..67 and 128..1023 (circle/memory.h COHERENT_SLOT_*); 68..127
// are free.
#define COHERENT_SLOT_KORA_MAILBOX 127

#ifdef __cplusplus
extern "C" {
#endif

// Return the 4 KB coherent page for the given slot index (page-aligned, stable
// across calls). Slots map to distinct pages in the coherent pool.
void *coherent_page(unsigned slot);

#ifdef __cplusplus
}
#endif
