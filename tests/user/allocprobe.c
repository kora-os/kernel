// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for user memory: page runs from alloc_pages, the libk
// malloc heap growing far past one pool, a nested ELF child while the heap is
// live, and deliberate leaks the kernel must reclaim when this task is reaped.
#include "../../user/libk/koraos.h"

#define SLOTS 96

static unsigned char *blocks[SLOTS];
static size_t sizes[SLOTS];
static unsigned rng;

static unsigned next_random(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static unsigned char pattern(unsigned slot, size_t i) {
    return (unsigned char)(slot * 73 + i * 37 + (i >> 9));
}

static int fail(const char *what, int code) {
    kputs("allocprobe: ");
    kputs(what);
    kputs("\n");
    return code;
}

static void fill(unsigned slot) {
    for (size_t i = 0; i < sizes[slot]; i++) {
        blocks[slot][i] = pattern(slot, i);
    }
}

static int intact(unsigned slot) {
    for (size_t i = 0; i < sizes[slot]; i++) {
        if (blocks[slot][i] != pattern(slot, i)) {
            return 0;
        }
    }
    return 1;
}

static int check_pages(void) {
    if (alloc_pages(0) != NULL) {
        return fail("alloc_pages(0) succeeded", 10);
    }
    if (free_pages((void *)0x1000) != -1) {
        return fail("free_pages accepted a foreign address", 11);
    }
    unsigned char *run = alloc_pages(32);  // 128 KiB, twice the old heap
    if (run == NULL) {
        return fail("alloc_pages(32) failed", 12);
    }
    for (size_t i = 0; i < 32 * KORAOS_PAGE_SIZE; i++) {
        if (run[i] != 0) {
            return fail("alloc_pages memory not zeroed", 13);
        }
        run[i] = (unsigned char)i;
    }
    if (free_pages(run) != 0 || free_pages(run) != -1) {
        return fail("free_pages did not free exactly once", 14);
    }
    return 0;
}

static int check_heap(void) {
    // Grow to about 1.5 MiB in mixed sizes, including direct page-run blocks.
    for (unsigned s = 0; s < SLOTS; s++) {
        unsigned kind = next_random() % 16;
        sizes[s] = kind == 0 ? 300 * 1024 + next_random() % 4096
                 : kind < 6 ? 1 + next_random() % 64
                 : 1 + next_random() % 12000;
        blocks[s] = malloc(sizes[s]);
        if (blocks[s] == NULL) {
            return fail("malloc failed", 20);
        }
        if (((unsigned long)blocks[s] & 15) != 0) {
            return fail("malloc block not 16-byte aligned", 21);
        }
        fill(s);
    }
    // Churn: free and reallocate random slots, checking each on the way out.
    for (unsigned r = 0; r < 600; r++) {
        unsigned s = next_random() % SLOTS;
        if (!intact(s)) {
            return fail("heap corrupted", 22);
        }
        if (next_random() % 3 == 0) {
            size_t grown = sizes[s] + 1 + next_random() % 2048;
            unsigned char *p = realloc(blocks[s], grown);
            if (p == NULL) {
                return fail("realloc failed", 23);
            }
            blocks[s] = p;
            for (size_t i = 0; i < sizes[s]; i++) {
                if (p[i] != pattern(s, i)) {
                    return fail("realloc lost contents", 24);
                }
            }
            sizes[s] = grown;
        } else {
            free(blocks[s]);
            sizes[s] = 1 + next_random() % 6000;
            blocks[s] = malloc(sizes[s]);
            if (blocks[s] == NULL) {
                return fail("malloc failed during churn", 25);
            }
        }
        fill(s);
    }

    int child = spawn("hello", 0, 0);
    if (child < 0 || wait(child) != 0) {
        return fail("nested child failed", 26);
    }

    struct kheap_info info;
    kheap_info(&info);
    if (info.pools < 2 || info.corrupt != 0) {
        return fail("unexpected pool layout", 27);
    }
    for (unsigned s = 0; s < SLOTS; s++) {
        if (!intact(s)) {
            return fail("heap corrupted across the child", 28);
        }
        free(blocks[s]);
    }

    unsigned *zeros = calloc(1000, sizeof(unsigned));
    if (zeros == NULL) {
        return fail("calloc failed", 29);
    }
    for (unsigned i = 0; i < 1000; i++) {
        if (zeros[i] != 0) {
            return fail("calloc memory not zeroed", 30);
        }
    }
    free(zeros);

    kheap_info(&info);
    if (info.used_blocks != 0 || info.direct_allocs != 0 || info.corrupt != 0 ||
        info.bad_frees != 0 || info.pools > 1) {
        return fail("heap not empty after freeing everything", 31);
    }
    return 0;
}

int main(void) {
    rng = (unsigned)getpid() * 2654435761u;
    int rc = check_pages();
    if (rc == 0) {
        rc = check_heap();
    }
    if (rc != 0) {
        return rc;
    }
    // Leave a page run and some heap behind: the kernel reclaims both when the
    // shell reaps this task (the smoke test checks the page count stays flat).
    if (alloc_pages(3) == NULL || malloc(5000) == NULL) {
        return fail("final allocations failed", 40);
    }
    kputs("allocprobe: heap checked\n");
    return 0;
}
