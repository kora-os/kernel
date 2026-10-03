// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Page runs handed to user programs by the alloc_pages/free_pages syscalls.
// With the flat identity map a run is directly usable at its physical address.
// Each run is recorded against its owning task, so whatever a program does not
// free itself is reclaimed when the task is reaped.

struct user_run {
    struct user_run *next;
    void *base;
    size_t pages;
};

struct task;

// Allocate `pages` contiguous zeroed pages for `t`. Returns NULL for a zero
// count or when memory (pages, or the kernel heap for the record) runs out.
void *user_pages_alloc(struct task *t, size_t pages);

// Free a run previously returned to `t` by user_pages_alloc(), by its base
// address. Returns 0, or -1 if `t` owns no run starting at `base`.
int user_pages_free(struct task *t, void *base);

// Free every run `t` still owns (task teardown).
void user_pages_release_all(struct task *t);

// Runs and pages `t` currently owns.
size_t user_pages_count(const struct task *t, size_t *runs);
