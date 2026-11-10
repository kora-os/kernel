// SPDX-License-Identifier: GPL-3.0-or-later
//
// Heap diagnostics for the unit tests (and debugging): not part of the public
// <stdlib.h>.
#ifndef LIBC_MALLOC_H
#define LIBC_MALLOC_H

#include <stddef.h>

#define MALLOC_PAGE_SIZE 4096

struct malloc_info {
    size_t pools;          // page-run pools backing malloc
    size_t pool_bytes;     // their total size
    size_t used_blocks;    // live malloc blocks in the pools
    size_t used_bytes;     // their size, headers included
    size_t free_bytes;     // free space in the pools
    size_t direct_allocs;  // large blocks with page runs of their own
    size_t direct_bytes;
    size_t bad_frees;      // misaligned or double frees ignored
    size_t corrupt;        // inconsistencies found in the pools (should be 0)
};

// Walks every pool under the heap lock and fills *out.
void __malloc_info(struct malloc_info *out);

#endif // LIBC_MALLOC_H
