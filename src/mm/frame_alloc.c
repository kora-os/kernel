#include "mm/frame_alloc.h"
#include "mm.h"

// Defined by the linker script: first byte after the kernel image.
extern char _end[];

// Keep early experiments inside the reserved LOW_MEMORY window so we never
// hand out pages beyond what we know exists.
#define FRAME_LIMIT ((uintptr_t)_end + (16 * 1024 * 1024))

static uintptr_t next_free;

static uintptr_t align_up(uintptr_t v, uintptr_t a) {
    return (v + a - 1) & ~(a - 1);
}

void frame_alloc_init(void) {
    next_free = align_up((uintptr_t)_end, PAGE_SIZE);
}

void *frame_alloc(void) {
    if (next_free + PAGE_SIZE > FRAME_LIMIT) {
        return NULL;
    }

    uintptr_t page = next_free;
    next_free += PAGE_SIZE;

    // Zero the page.
    volatile uint64_t *p = (volatile uint64_t *)page;
    for (unsigned i = 0; i < PAGE_SIZE / sizeof(uint64_t); i++) {
        p[i] = 0;
    }

    return (void *)page;
}
