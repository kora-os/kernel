// SPDX-License-Identifier: GPL-3.0-or-later
//
// KoraOS bridge for the few Circle memory/runtime hooks the USB stack needs.
// operator new/delete are already provided kernel-wide (src/cxx/cxx_runtime.cpp);
// here we cover coherent DMA pages, the page allocator, the assertion handler,
// and the busy-loop delay.

#include <circle/memory.h>

#include "mm.h"
#include "mm/frame_alloc.h"

extern "C" void tfp_printf(const char *fmt, ...);

// Coherent pages requested by slot (e.g. the property-mailbox buffer). Backed by
// ordinary frame-allocator pages here; buffers shared with the VideoCore are
// kept coherent by explicit cache maintenance around the transfer (real-hardware
// concern handled in the Pi 3 bring-up).
uintptr CMemorySystem::GetCoherentPage(unsigned nSlot) {
    static void *slots[64];
    if (nSlot >= 64) {
        return 0;
    }
    if (slots[nSlot] == 0) {
        slots[nSlot] = frame_alloc();
    }
    return (uintptr)slots[nSlot];
}

extern "C" {

// Circle's page allocator hooks: single 4 KB pages from the frame allocator.
void *palloc(void) {
    return frame_alloc();
}

void pfree(void *pPage) {
    frame_free(pPage);
}

// Circle's assert() calls this on failure.
void assertion_failed(const char *pExpr, const char *pFile, unsigned nLine) {
    tfp_printf("circle assert failed: %s at %s:%u\n", pExpr, pFile, nLine);
    for (;;) {
        asm volatile("wfi");
    }
}

// Busy-loop delay referenced by CTimer::nsDelay (unused on the fast path, but
// keep the symbol defined).
void DelayLoop(unsigned nCount) {
    for (volatile unsigned i = 0; i < nCount; i++) {
    }
}

}  // extern "C"
