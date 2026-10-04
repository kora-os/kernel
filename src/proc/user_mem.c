// SPDX-License-Identifier: GPL-3.0-or-later
//
// Per-task user page runs. See proc/user_mem.h.

#include "proc/user_mem.h"

#include "mm/frame_alloc.h"
#include "mm/kmalloc.h"
#include "proc/task.h"

void *user_pages_alloc(task_t *t, size_t pages) {
    if (pages == 0) {
        return NULL;
    }
    struct user_run *run = kmalloc(sizeof(*run));
    if (run == NULL) {
        return NULL;
    }
    void *base = frame_alloc_pages(pages);
    if (base == NULL) {
        kfree(run);
        return NULL;
    }
    run->base = base;
    run->pages = pages;
    run->next = t->runs;
    t->runs = run;
    return base;
}

int user_pages_free(task_t *t, void *base) {
    for (struct user_run **link = &t->runs; *link != NULL; link = &(*link)->next) {
        struct user_run *run = *link;
        if (run->base == base) {
            *link = run->next;
            frame_free_pages(run->base, run->pages);
            kfree(run);
            return 0;
        }
    }
    return -1;
}

void user_pages_release_all(task_t *t) {
    while (t->runs != NULL) {
        struct user_run *run = t->runs;
        t->runs = run->next;
        frame_free_pages(run->base, run->pages);
        kfree(run);
    }
}

size_t user_pages_count(const task_t *t, size_t *runs) {
    size_t pages = 0;
    size_t n = 0;
    for (const struct user_run *run = t->runs; run != NULL; run = run->next) {
        pages += run->pages;
        n++;
    }
    if (runs != NULL) {
        *runs = n;
    }
    return pages;
}
