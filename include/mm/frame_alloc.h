#pragma once

#include "common.h"

// Physical 4 KB page allocator for the reserved low-memory pool that begins
// just past the kernel image (_end) and runs for a fixed window. Backed by a
// bitmap, so pages can be freed and reused -- enough to back user program
// images, stacks, and heaps that come and go as tasks spawn and exit. With the
// flat identity map, returned pointers are usable as-is by both the kernel and
// EL0.
void frame_alloc_init(void);

// Allocate one zeroed 4 KB page. Returns NULL if the pool is exhausted.
void *frame_alloc(void);

// Allocate `count` contiguous zeroed 4 KB pages. Returns a pointer to the first
// page, or NULL if no run of that length is free. `count == 0` returns NULL.
void *frame_alloc_pages(size_t count);

// Free a single page previously returned by frame_alloc().
void frame_free(void *page);

// Free `count` contiguous pages previously returned by frame_alloc_pages()
// (pass the same pointer and count). Freeing something not from the pool, or a
// mismatched range, is ignored.
void frame_free_pages(void *pages, size_t count);
