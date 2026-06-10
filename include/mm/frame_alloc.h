#pragma once

#include "common.h"

// Trivial bump allocator for 4 KB physical pages, starting just past the
// kernel image (_end). No free() yet -- enough to back a user stack during
// bring-up. With the flat identity map, returned pointers are usable as-is by
// both the kernel and EL0.
void frame_alloc_init(void);

// Allocate one zeroed 4 KB page. Returns NULL if exhausted.
void *frame_alloc(void);
