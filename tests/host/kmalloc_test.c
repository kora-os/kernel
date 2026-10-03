// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the kernel heap (src/mm/kmalloc.c) and its stress routine
// (src/mm/kmalloc_stress.c). The frame allocator is stubbed with page-aligned
// host memory, with a page budget to simulate exhaustion; the IRQ mask is
// stubbed with a nesting counter to check every critical section is balanced.

#include "mm.h"
#include "mm/frame_alloc.h"
#include "mm/kmalloc.h"
#include "test.h"

int posix_memalign(void **out, size_t align, size_t size);
void free(void *ptr);
void *memset(void *dest, int c, size_t n);

// Frame allocator stub ---------------------------------------------------

#define MAX_RUNS 4096

static struct {
    void *base;
    size_t pages;
} runs[MAX_RUNS];
static size_t pages_live;
static size_t page_budget = (size_t)-1;
static int bad_page_frees;

void *frame_alloc_pages(size_t count) {
    if (count == 0 || pages_live + count > page_budget) {
        return NULL;
    }
    for (unsigned i = 0; i < MAX_RUNS; i++) {
        if (runs[i].base == NULL) {
            void *p = NULL;
            if (posix_memalign(&p, PAGE_SIZE, count * PAGE_SIZE) != 0) {
                return NULL;
            }
            memset(p, 0, count * PAGE_SIZE);
            runs[i].base = p;
            runs[i].pages = count;
            pages_live += count;
            return p;
        }
    }
    return NULL;
}

void *frame_alloc(void) {
    return frame_alloc_pages(1);
}

void frame_free_pages(void *pages, size_t count) {
    for (unsigned i = 0; i < MAX_RUNS; i++) {
        if (runs[i].base == pages) {
            if (runs[i].pages != count) {
                bad_page_frees++;
            }
            free(pages);
            runs[i].base = NULL;
            pages_live -= runs[i].pages;
            return;
        }
    }
    bad_page_frees++;
}

void frame_free(void *page) {
    frame_free_pages(page, 1);
}

// IRQ mask and console stubs -----------------------------------------------

static int irq_depth;
static int irq_max_depth;

uint64_t irq_save(void) {
    irq_depth++;
    if (irq_depth > irq_max_depth) {
        irq_max_depth = irq_depth;
    }
    return (uint64_t)(irq_depth - 1);
}

void irq_restore(uint64_t flags) {
    irq_depth--;
    if ((uint64_t)irq_depth != flags) {
        irq_depth = -1000;  // unbalanced: caught by check_balanced()
    }
}

static int reports;

void tfp_printf(char *fmt, ...) {
    (void)fmt;
    reports++;
}

static void check_balanced(const char *where) {
    CHECK(irq_depth == 0, "%s: IRQ mask left unbalanced (%d)", where, irq_depth);
}

static size_t live_allocs(void) {
    struct kmalloc_stats st;
    kmalloc_get_stats(&st);
    return st.live_allocs;
}

// Tests ------------------------------------------------------------------

static void test_alignment_and_zeroing(void) {
    static uint8_t *blocks[5000 / 7 + 1];
    unsigned n = 0;
    for (size_t size = 0; size <= 5000; size += 7) {
        uint8_t *p = kmalloc(size);
        CHECK(p != NULL, "kmalloc(%zu) failed", size);
        if (p == NULL) {
            continue;
        }
        CHECK(((uintptr_t)p & (KMALLOC_MIN_ALIGN - 1)) == 0, "kmalloc(%zu) misaligned", size);
        bool zero = true;
        for (size_t i = 0; i < size; i++) {
            zero = zero && p[i] == 0;
        }
        CHECK(zero, "kmalloc(%zu) not zeroed", size);
        memset(p, 0xa5, size);
        blocks[n++] = p;
    }
    for (unsigned i = 0; i < n; i++) {
        kfree(blocks[i]);
    }
    CHECK(live_allocs() == 0, "leaked %zu allocations", live_allocs());
    check_balanced("alignment");
}

// No two live blocks overlap, and padding keeps them in separate cache lines.
static void test_blocks_do_not_share_lines(void) {
    enum { N = 200 };
    static uint8_t *p[N];
    static size_t sz[N];
    for (unsigned i = 0; i < N; i++) {
        sz[i] = 1 + (i * 37) % 1400;
        p[i] = kmalloc(sz[i]);
        CHECK(p[i] != NULL, "kmalloc failed");
    }
    for (unsigned i = 0; i < N; i++) {
        uintptr_t a0 = (uintptr_t)p[i];
        uintptr_t a1 = (a0 + sz[i] + 63) & ~(uintptr_t)63;
        for (unsigned j = i + 1; j < N; j++) {
            uintptr_t b0 = (uintptr_t)p[j];
            uintptr_t b1 = (b0 + sz[j] + 63) & ~(uintptr_t)63;
            CHECK(a1 <= b0 || b1 <= a0, "blocks %u and %u share a cache line", i, j);
        }
    }
    for (unsigned i = 0; i < N; i++) {
        kfree(p[i]);
    }
    CHECK(live_allocs() == 0, "leaked %zu allocations", live_allocs());
}

static void test_reuse_and_zero_on_reuse(void) {
    uint8_t *a = kmalloc(100);
    memset(a, 0xff, 100);
    kfree(a);
    uint8_t *b = kmalloc(120);  // same 128-byte class
    CHECK(a == b, "freed block not reused (LIFO)");
    bool zero = true;
    for (unsigned i = 0; i < 120; i++) {
        zero = zero && b[i] == 0;
    }
    CHECK(zero, "reused block not zeroed (free poison leaked)");
    kfree(b);
}

static void test_slab_pages_released(void) {
    enum { N = 63 * 5 };  // five full 64-byte slabs
    static void *p[N];
    size_t base = pages_live;
    for (unsigned i = 0; i < N; i++) {
        p[i] = kmalloc(64);
    }
    struct kmalloc_stats st;
    kmalloc_get_stats(&st);
    CHECK(st.classes[0].used == N, "class 0 used %zu", st.classes[0].used);
    CHECK(st.classes[0].slabs >= 5, "class 0 slabs %zu", st.classes[0].slabs);
    for (unsigned i = 0; i < N; i++) {
        kfree(p[i]);
    }
    kmalloc_get_stats(&st);
    CHECK(st.classes[0].used == 0, "class 0 still used %zu", st.classes[0].used);
    CHECK(st.classes[0].slabs <= 1, "%zu empty slabs kept, want at most 1",
          st.classes[0].slabs);
    CHECK(pages_live <= base, "pages not returned: %zu -> %zu", base, pages_live);
}

static void test_large_allocations(void) {
    size_t base = pages_live;
    uint8_t *a = kmalloc(KMALLOC_MAX_SMALL + 1);
    CHECK(pages_live == base + 1, "1345 bytes should take one page");
    uint8_t *b = kmalloc(PAGE_SIZE);
    CHECK(pages_live == base + 3, "4096 bytes plus header should take two pages");
    uint8_t *c = kmalloc(1 << 20);
    CHECK(c != NULL, "1 MiB allocation failed");
    memset(c, 0x5a, 1 << 20);
    struct kmalloc_stats st;
    kmalloc_get_stats(&st);
    CHECK(st.large_allocs == 3, "large allocs %zu", st.large_allocs);
    kfree(b);
    kfree(a);
    kfree(c);
    CHECK(pages_live == base, "large pages leaked: %zu", pages_live - base);
    CHECK(bad_page_frees == 0, "frame frees with the wrong run length");
    check_balanced("large");
}

static void test_aligned_allocations(void) {
    size_t base = pages_live;
    for (size_t align = 1; align <= 65536; align <<= 1) {
        for (size_t size = 1; size < 20000; size = size * 3 + 5) {
            uint8_t *p = kmalloc_aligned(size, align);
            CHECK(p != NULL, "aligned(%zu, %zu) failed", size, align);
            size_t want = align < KMALLOC_MIN_ALIGN ? KMALLOC_MIN_ALIGN : align;
            CHECK(((uintptr_t)p & (want - 1)) == 0, "aligned(%zu, %zu) = %p", size, align,
                  (void *)p);
            memset(p, 0x3c, size);  // ASan catches writes outside the run
            kfree(p);
        }
    }
    CHECK(kmalloc_aligned(64, 3) == NULL, "non power-of-two alignment accepted");
    CHECK(kmalloc_aligned(64, 0) == NULL, "zero alignment accepted");
    CHECK(kmalloc_aligned((size_t)-100, 64) == NULL, "overflowing size accepted");
    CHECK(pages_live <= base + KMALLOC_CLASSES, "aligned pages leaked");
    check_balanced("aligned");
}

static void test_exhaustion(void) {
    void *keep = kmalloc(64);  // a cached slab may exist; use it
    struct kmalloc_stats before;
    kmalloc_get_stats(&before);
    page_budget = pages_live;  // nothing more available
    CHECK(kmalloc(PAGE_SIZE) == NULL, "large allocation succeeded with no pages");
    void *p[64];
    unsigned got = 0;
    for (unsigned i = 0; i < 64; i++) {
        p[i] = kmalloc(1000);
        if (p[i] != NULL) {
            got++;
        }
    }
    struct kmalloc_stats after;
    kmalloc_get_stats(&after);
    CHECK(after.failed_allocs > before.failed_allocs, "failures not counted");
    page_budget = (size_t)-1;
    for (unsigned i = 0; i < 64; i++) {
        kfree(p[i]);
    }
    kfree(keep);
    void *again = kmalloc(1000);
    CHECK(again != NULL, "heap unusable after exhaustion");
    kfree(again);
    CHECK(live_allocs() == 0, "leaked %zu allocations", live_allocs());
    check_balanced("exhaustion");
    (void)got;
}

static void test_bad_frees(void) {
    struct kmalloc_stats st;
    kfree(NULL);
    kmalloc_get_stats(&st);
    size_t bad = st.bad_frees;

    uint8_t *a = kmalloc(200);
    uint8_t *keep = kmalloc(200);  // keeps the slab alive after `a` is freed
    kfree(a + 8);
    kfree(a + 64);  // aligned, but not a block start in the 256-byte class
    kfree(a);
    kfree(a);  // double free
    uint8_t *big = kmalloc(9000);
    kfree(big + 64);
    kmalloc_get_stats(&st);
    CHECK(st.bad_frees == bad + 4, "bad frees counted %zu, want %zu", st.bad_frees, bad + 4);
    CHECK(reports >= 4, "bad frees not reported");
    kfree(big);
    kfree(keep);
    CHECK(live_allocs() == 0, "leaked %zu allocations", live_allocs());
    check_balanced("bad frees");
}

static void test_stress(void) {
    size_t base = pages_live;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        int rc = kmalloc_stress(5000, seed);
        CHECK(rc == 0, "stress seed %u failed check %d", seed, rc);
    }
    CHECK(live_allocs() == 0, "stress leaked %zu allocations", live_allocs());
    CHECK(pages_live <= base + KMALLOC_CLASSES, "stress leaked pages: %zu -> %zu", base,
          pages_live);
    CHECK(irq_max_depth >= 1, "heap never masked IRQs");
    check_balanced("stress");
}

TEST_MAIN(test_alignment_and_zeroing, test_blocks_do_not_share_lines,
          test_reuse_and_zero_on_reuse, test_slab_pages_released, test_large_allocations,
          test_aligned_allocations, test_exhaustion, test_bad_frees, test_stress)
