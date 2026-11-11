// SPDX-License-Identifier: GPL-3.0-or-later
//
// kheap_info(): heap diagnostics for KoraOS programs and tests (allocprobe),
// from the libc's own walk of its malloc pools. libk sits next to the libc in
// this tree, so it uses the libc's internal diagnostics header; the fields
// are copied one by one, so the two structs need not share a layout.

#include "koraos.h"

#include "../libc/src/internal/malloc.h"

void kheap_info(struct kheap_info *out) {
    struct malloc_info m;
    __malloc_info(&m);
    out->pools = m.pools;
    out->pool_bytes = m.pool_bytes;
    out->used_blocks = m.used_blocks;
    out->used_bytes = m.used_bytes;
    out->free_bytes = m.free_bytes;
    out->direct_allocs = m.direct_allocs;
    out->direct_bytes = m.direct_bytes;
    out->bad_frees = m.bad_frees;
    out->corrupt = m.corrupt;
}
