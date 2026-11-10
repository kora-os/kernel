// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal lock for data the threads of a program share (the heap). A
// test-and-set lock on compiler atomics (load-acquire/store-exclusive loops on
// the Cortex-A53, which has no LSE atomics). A waiter spins briefly, which is
// enough when the holder runs on another core, then yields its core, in case
// the holder was preempted on this very core and can only finish once it gets
// the CPU back.
#ifndef LIBC_LOCK_H
#define LIBC_LOCK_H

#include "syscall.h"

#define LOCK_SPINS 100  // spin-wait rounds before giving the core away

typedef struct {
    int locked;
} lock_t;

#define LOCK_INIT {0}

static inline void cpu_relax(void) {
#if defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

static inline int __trylock(lock_t *l) {
    return __atomic_exchange_n(&l->locked, 1, __ATOMIC_SEQ_CST) == 0;
}

static inline void __lock(lock_t *l) {
    unsigned spins = 0;
    while (!__trylock(l)) {
        // Wait with plain loads until it looks free, then try again.
        while (__atomic_load_n(&l->locked, __ATOMIC_ACQUIRE)) {
            if (++spins < LOCK_SPINS) {
                cpu_relax();
            } else {
                spins = 0;
                __syscall0(SYS_YIELD);
            }
        }
    }
}

static inline void __unlock(lock_t *l) {
    __atomic_store_n(&l->locked, 0, __ATOMIC_RELEASE);
}

#endif // LIBC_LOCK_H
