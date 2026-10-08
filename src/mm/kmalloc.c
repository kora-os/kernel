// SPDX-License-Identifier: GPL-3.0-or-later
//
// Kernel heap: size-class slabs for small objects, page runs for large ones.
// See mm/kmalloc.h.
//
// Every page the heap owns starts with a 64-byte header (struct kheap_page).
// A slab is one page: the header, then equal blocks of its class size, each a
// multiple of KMALLOC_MIN_ALIGN. A large allocation is a run of pages with the
// header in the page just before the returned pointer. Either way, kfree()
// finds the header at the start of the page holding the byte before `ptr`:
// slab blocks start at least 64 bytes into their page, and a large block's
// pointer is placed at least 64 bytes past its header.

#include "mm/kmalloc.h"

#include "arch/spinlock.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "mm.h"
#include "mm/frame_alloc.h"

#define HEADER_SIZE 64
#define SLAB_PAYLOAD (PAGE_SIZE - HEADER_SIZE)

#define MAGIC_SLAB 0x4b534c42u   // "KSLB"
#define MAGIC_LARGE 0x4b4c5247u  // "KLRG"

// Written into the second word of a free slab block (the first holds the free
// list link), so a second kfree() of the same block is caught. Cleared when the
// block is handed out again, since allocations are zeroed.
#define FREE_POISON 0xf7eeb10cdeadf7eeull

struct kheap_page {
    uint32_t magic;
    uint32_t pad;
    union {
        struct {
            uint16_t cls;     // index into class_sizes
            uint16_t used;    // blocks handed out
            uint16_t fresh;   // blocks [fresh, capacity) were never handed out
            uint16_t pad;
            void *free;       // free list of returned blocks
            struct kheap_page *prev, *next;  // the class's partial list
        } slab;
        struct {
            uintptr_t base;   // first page of the run
            size_t pages;     // run length
            void *data;       // the pointer handed out
        } large;
    };
};

_Static_assert(sizeof(struct kheap_page) <= HEADER_SIZE, "heap page header too large");

struct free_block {
    struct free_block *next;
    uint64_t poison;
};

// Block sizes are multiples of 64; several divide the 4032-byte payload
// exactly (64, 192, 576, 1344), the rest are the common power-of-two sizes.
static const uint16_t class_sizes[KMALLOC_CLASSES] = {
    64, 128, 192, 256, 384, 576, 768, 1024, KMALLOC_MAX_SMALL,
};

_Static_assert(KMALLOC_MAX_SMALL <= SLAB_PAYLOAD, "largest class must fit a slab");

struct kheap_class {
    struct kheap_page *partial;  // slabs with at least one free block
    size_t slabs;
    size_t used;
    bool has_empty;              // one fully free slab is kept cached
};

// heap_lock (IRQ-safe: Circle allocates in USB interrupt handlers) covers all
// heap state. Lock order: heap, then frames (slabs and runs come from there).
static struct spinlock heap_lock = SPINLOCK_INIT("heap");

static struct kheap_class classes[KMALLOC_CLASSES];
static size_t large_allocs;
static size_t large_pages;
static size_t total_allocs;
static size_t failed_allocs;
static size_t bad_frees;

static size_t class_capacity(unsigned cls) {
    return SLAB_PAYLOAD / class_sizes[cls];
}

static uint8_t *block_at(struct kheap_page *page, unsigned cls, size_t index) {
    return (uint8_t *)page + HEADER_SIZE + index * class_sizes[cls];
}

static void partial_push(struct kheap_class *c, struct kheap_page *page) {
    page->slab.prev = NULL;
    page->slab.next = c->partial;
    if (c->partial != NULL) {
        c->partial->slab.prev = page;
    }
    c->partial = page;
}

static void partial_unlink(struct kheap_class *c, struct kheap_page *page) {
    if (page->slab.prev != NULL) {
        page->slab.prev->slab.next = page->slab.next;
    } else {
        c->partial = page->slab.next;
    }
    if (page->slab.next != NULL) {
        page->slab.next->slab.prev = page->slab.prev;
    }
    page->slab.prev = page->slab.next = NULL;
}

static void *slab_alloc(unsigned cls) {
    struct kheap_class *c = &classes[cls];
    struct kheap_page *page = c->partial;
    if (page == NULL) {
        page = frame_alloc();
        if (page == NULL) {
            return NULL;
        }
        page->magic = MAGIC_SLAB;
        page->slab.cls = (uint16_t)cls;
        c->slabs++;
        c->has_empty = true;
        partial_push(c, page);
    }

    uint8_t *block;
    if (page->slab.free != NULL) {
        struct free_block *fb = page->slab.free;
        page->slab.free = fb->next;
        block = (uint8_t *)fb;
    } else {
        block = block_at(page, cls, page->slab.fresh++);
    }
    if (page->slab.used == 0) {
        c->has_empty = false;
    }
    page->slab.used++;
    c->used++;
    if (page->slab.used == class_capacity(cls)) {
        partial_unlink(c, page);
    }
    memset(block, 0, class_sizes[cls]);
    return block;
}

static void *large_alloc(size_t size, size_t align) {
    // The pointer lands `align` (at least HEADER_SIZE) bytes into the run at
    // most, so that much slack in front always fits the header.
    size_t lead = align < HEADER_SIZE ? HEADER_SIZE : align;
    if (size > (size_t)-1 - lead - PAGE_SIZE) {
        return NULL;
    }
    size_t pages = (size + lead + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t base = (uintptr_t)frame_alloc_pages(pages);
    if (base == 0) {
        return NULL;
    }
    uintptr_t ptr = (base + HEADER_SIZE + lead - 1) & ~(uintptr_t)(lead - 1);
    struct kheap_page *hdr = (struct kheap_page *)((ptr - 1) & ~(uintptr_t)(PAGE_SIZE - 1));
    hdr->magic = MAGIC_LARGE;
    hdr->large.base = base;
    hdr->large.pages = pages;
    hdr->large.data = (void *)ptr;
    large_allocs++;
    large_pages += pages;
    return (void *)ptr;
}

void *kmalloc_aligned(size_t size, size_t align) {
    if (align == 0 || (align & (align - 1)) != 0) {
        return NULL;
    }

    uint64_t flags = spin_lock_irqsave(&heap_lock);
    void *ptr = NULL;
    if (align <= KMALLOC_MIN_ALIGN && size <= KMALLOC_MAX_SMALL) {
        unsigned cls = 0;
        while (class_sizes[cls] < size) {
            cls++;
        }
        ptr = slab_alloc(cls);
    } else {
        ptr = large_alloc(size, align);
    }
    if (ptr != NULL) {
        total_allocs++;
    } else {
        failed_allocs++;
    }
    spin_unlock_irqrestore(&heap_lock, flags);
    return ptr;
}

void *kmalloc(size_t size) {
    return kmalloc_aligned(size, KMALLOC_MIN_ALIGN);
}

static void report_bad_free(void *ptr, const char *why) {
    bad_frees++;
    printf("kfree: %s pointer 0x%lx ignored\n", why, (unsigned long)(uintptr_t)ptr);
}

static void slab_free(struct kheap_page *page, uint8_t *block) {
    unsigned cls = page->slab.cls;
    if (cls >= KMALLOC_CLASSES) {
        report_bad_free(block, "corrupt slab for");
        return;
    }
    size_t offset = (size_t)(block - block_at(page, cls, 0));
    if (block < block_at(page, cls, 0) || offset % class_sizes[cls] != 0 ||
        offset / class_sizes[cls] >= page->slab.fresh) {
        report_bad_free(block, "unknown");
        return;
    }
    struct free_block *fb = (struct free_block *)block;
    if (fb->poison == FREE_POISON) {
        report_bad_free(block, "double free of");
        return;
    }

    struct kheap_class *c = &classes[cls];
    if (page->slab.used == class_capacity(cls)) {
        partial_push(c, page);
    }
    fb->next = page->slab.free;
    fb->poison = FREE_POISON;
    page->slab.free = fb;
    page->slab.used--;
    c->used--;

    if (page->slab.used == 0) {
        if (!c->has_empty) {
            c->has_empty = true;
        } else {
            partial_unlink(c, page);
            page->magic = 0;
            c->slabs--;
            frame_free(page);
        }
    }
}

static void large_free(struct kheap_page *hdr, void *ptr) {
    if (hdr->large.data != ptr) {
        report_bad_free(ptr, "unknown");
        return;
    }
    uintptr_t base = hdr->large.base;
    size_t pages = hdr->large.pages;
    hdr->magic = 0;
    large_allocs--;
    large_pages -= pages;
    frame_free_pages((void *)base, pages);
}

void kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    uint64_t flags = spin_lock_irqsave(&heap_lock);
    if (((uintptr_t)ptr & (KMALLOC_MIN_ALIGN - 1)) != 0) {
        report_bad_free(ptr, "misaligned");
    } else {
        struct kheap_page *hdr =
            (struct kheap_page *)(((uintptr_t)ptr - 1) & ~(uintptr_t)(PAGE_SIZE - 1));
        if (hdr->magic == MAGIC_SLAB) {
            slab_free(hdr, ptr);
        } else if (hdr->magic == MAGIC_LARGE) {
            large_free(hdr, ptr);
        } else {
            report_bad_free(ptr, "unknown");
        }
    }
    spin_unlock_irqrestore(&heap_lock, flags);
}

void kmalloc_get_stats(struct kmalloc_stats *out) {
    uint64_t flags = spin_lock_irqsave(&heap_lock);
    size_t live = large_allocs;
    for (unsigned i = 0; i < KMALLOC_CLASSES; i++) {
        out->classes[i].block_size = class_sizes[i];
        out->classes[i].slabs = classes[i].slabs;
        out->classes[i].used = classes[i].used;
        out->classes[i].capacity = classes[i].slabs * class_capacity(i);
        live += classes[i].used;
    }
    out->large_allocs = large_allocs;
    out->large_pages = large_pages;
    out->live_allocs = live;
    out->total_allocs = total_allocs;
    out->failed_allocs = failed_allocs;
    out->bad_frees = bad_frees;
    spin_unlock_irqrestore(&heap_lock, flags);
}
