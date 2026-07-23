#include "mm/frame_alloc.h"
#include "mm.h"

// Defined by the linker script: first byte after the kernel image.
extern char _end[];

// Size of the reserved physical pool that starts just past the kernel image.
// Everything the frame allocator hands out lives inside this window, so we
// never touch RAM we have not confirmed exists.
#define POOL_BYTES (16 * 1024 * 1024)
#define MAX_FRAMES (POOL_BYTES / PAGE_SIZE)
#define BITS_PER_WORD 64
#define BITMAP_WORDS ((MAX_FRAMES + BITS_PER_WORD - 1) / BITS_PER_WORD)

// One bit per page: 1 = allocated, 0 = free.
static uint64_t bitmap[BITMAP_WORDS];
static uintptr_t pool_base;
static size_t pool_frames;

static uintptr_t align_up(uintptr_t v, uintptr_t a) {
    return (v + a - 1) & ~(a - 1);
}

static bool frame_is_used(size_t i) {
    return (bitmap[i / BITS_PER_WORD] >> (i % BITS_PER_WORD)) & 1u;
}

static void frame_set_used(size_t i) {
    bitmap[i / BITS_PER_WORD] |= (uint64_t)1 << (i % BITS_PER_WORD);
}

static void frame_set_free(size_t i) {
    bitmap[i / BITS_PER_WORD] &= ~((uint64_t)1 << (i % BITS_PER_WORD));
}

static void zero_pages(void *page, size_t count) {
    volatile uint64_t *p = (volatile uint64_t *)page;
    size_t words = (count * PAGE_SIZE) / sizeof(uint64_t);
    for (size_t i = 0; i < words; i++) {
        p[i] = 0;
    }
}

void frame_alloc_init(void) {
    pool_base = align_up((uintptr_t)_end, PAGE_SIZE);

    // The pool ends a fixed distance past the (unaligned) kernel end; the
    // alignment above may cost us up to one frame, so derive the count from the
    // aligned base rather than assuming the maximum.
    uintptr_t pool_end = (uintptr_t)_end + POOL_BYTES;
    pool_frames = (pool_end - pool_base) / PAGE_SIZE;
    if (pool_frames > MAX_FRAMES) {
        pool_frames = MAX_FRAMES;
    }

    for (size_t i = 0; i < BITMAP_WORDS; i++) {
        bitmap[i] = 0;
    }
}

void *frame_alloc_pages(size_t count) {
    if (count == 0 || count > pool_frames) {
        return NULL;
    }

    // Linear first-fit scan for a run of `count` consecutive free frames.
    for (size_t start = 0; start + count <= pool_frames; start++) {
        size_t run = 0;
        while (run < count && !frame_is_used(start + run)) {
            run++;
        }
        if (run == count) {
            for (size_t i = 0; i < count; i++) {
                frame_set_used(start + i);
            }
            void *page = (void *)(pool_base + start * PAGE_SIZE);
            zero_pages(page, count);
            return page;
        }
        // Skip past the used frame that broke the run.
        start += run;
    }

    return NULL;
}

void *frame_alloc(void) {
    return frame_alloc_pages(1);
}

void frame_free_pages(void *pages, size_t count) {
    if (pages == NULL || count == 0) {
        return;
    }

    uintptr_t addr = (uintptr_t)pages;
    if (addr < pool_base || (addr - pool_base) % PAGE_SIZE != 0) {
        return;  // Not a page from this pool.
    }

    size_t start = (addr - pool_base) / PAGE_SIZE;
    if (start >= pool_frames || start + count > pool_frames) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        frame_set_free(start + i);
    }
}

void frame_free(void *page) {
    frame_free_pages(page, 1);
}
