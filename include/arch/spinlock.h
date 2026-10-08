// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Spinlocks on ARMv8.0 exclusives (the Cortex-A53 has no LSE atomics):
// load-acquire exclusive / store exclusive to take the lock, waiting in WFE
// between attempts, and a store-release to drop it (which also wakes the
// waiters' WFE through the exclusive monitor).
//
// Locking rules (see docs/locking.md):
//   * Data an interrupt handler also touches is locked with the _irqsave
//     variants: masking IRQs on this core keeps a handler from deadlocking on a
//     lock its own core holds; the lock keeps other cores out.
//   * Never sleep (block, or switch tasks) while holding a spinlock; the
//     scheduler checks this.
//   * Taking a lock this core already holds is a bug and panics, rather than
//     spinning forever.

struct spinlock {
    volatile uint32_t locked;
    uint32_t owner;       // holding core + 1, or 0 (diagnostics)
    const char *name;
};

#define SPINLOCK_INIT(lock_name) { 0, 0, lock_name }

void spin_lock(struct spinlock *lock);
bool spin_trylock(struct spinlock *lock);
void spin_unlock(struct spinlock *lock);
bool spin_is_held_here(const struct spinlock *lock);

// Mask IRQs on this core, then take the lock; returns the previous mask.
uint64_t spin_lock_irqsave(struct spinlock *lock);
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags);
