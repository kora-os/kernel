// SPDX-License-Identifier: GPL-3.0-or-later
//
// Deterministic kernel heap stress, shared by the host unit test, the boot
// self-test and the debug console's `heaptest`. See kmalloc_stress() in
// mm/kmalloc.h.

#include "mm/kmalloc.h"

#include "mm.h"

#define SLOTS 48

struct slot {
    uint8_t *ptr;
    size_t size;
    size_t align;
    uint8_t tag;
};

// Not reentrant: callers run it from one context at a time.
static struct slot slots[SLOTS];

static uint32_t next_random(uint32_t *state) {
    uint32_t x = *state;  // xorshift32
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint8_t pattern(uint8_t tag, size_t i) {
    return (uint8_t)(tag ^ (i * 31u) ^ (i >> 8));
}

// Mostly slab sizes, some page runs, some over-aligned blocks.
static void pick_shape(uint32_t *rng, size_t *size, size_t *align) {
    uint32_t kind = next_random(rng) % 10;
    *align = KMALLOC_MIN_ALIGN;
    if (kind < 7) {
        *size = next_random(rng) % (KMALLOC_MAX_SMALL + 1);
    } else if (kind < 9) {
        *size = KMALLOC_MAX_SMALL + 1 + next_random(rng) % 12000;
    } else {
        *size = 1 + next_random(rng) % 6000;
        *align = (size_t)1 << (next_random(rng) % 9);  // 1 to 256 bytes
        if (next_random(rng) % 4 == 0) {
            *align = (size_t)PAGE_SIZE << (next_random(rng) % 2);
        }
    }
}

static int check_and_free(struct slot *s) {
    for (size_t i = 0; i < s->size; i++) {
        if (s->ptr[i] != pattern(s->tag, i)) {
            return 4;
        }
    }
    kfree(s->ptr);
    s->ptr = NULL;
    return 0;
}

int kmalloc_stress(unsigned rounds, uint32_t seed) {
    struct kmalloc_stats before;
    struct kmalloc_stats after;
    kmalloc_get_stats(&before);

    uint32_t rng = seed != 0 ? seed : 0x2545f491u;
    int rc = 0;
    for (unsigned i = 0; i < SLOTS; i++) {
        slots[i].ptr = NULL;
    }

    for (unsigned r = 0; r < rounds && rc == 0; r++) {
        struct slot *s = &slots[next_random(&rng) % SLOTS];
        if (s->ptr != NULL) {
            rc = check_and_free(s);
            continue;
        }
        pick_shape(&rng, &s->size, &s->align);
        s->ptr = kmalloc_aligned(s->size, s->align);
        if (s->ptr == NULL) {
            rc = 1;
            break;
        }
        size_t need = s->align < KMALLOC_MIN_ALIGN ? KMALLOC_MIN_ALIGN : s->align;
        if (((uintptr_t)s->ptr & (need - 1)) != 0) {
            rc = 2;
            break;
        }
        s->tag = (uint8_t)next_random(&rng);
        for (size_t i = 0; i < s->size; i++) {
            if (s->ptr[i] != 0) {
                rc = 3;
                break;
            }
            s->ptr[i] = pattern(s->tag, i);
        }
    }

    for (unsigned i = 0; i < SLOTS; i++) {
        if (slots[i].ptr != NULL) {
            int frc = check_and_free(&slots[i]);
            if (rc == 0) {
                rc = frc;
            }
            slots[i].ptr = NULL;
        }
    }

    kmalloc_get_stats(&after);
    if (rc == 0 && (after.live_allocs != before.live_allocs ||
                    after.large_pages != before.large_pages)) {
        rc = 5;
    }
    if (rc == 0 && after.bad_frees != before.bad_frees) {
        rc = 6;
    }
    return rc;
}
