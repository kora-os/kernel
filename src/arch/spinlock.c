// SPDX-License-Identifier: GPL-3.0-or-later
//
// Spinlocks. See arch/spinlock.h.

#include "arch/spinlock.h"

#include "arch/irq.h"
#include "arch/percpu.h"
#include "lib/panic.h"

#if defined(__aarch64__) && !defined(SPINLOCK_PORTABLE)
// Host tests on an arm64 host run the same exclusives, with YIELD for WFE
// (WFE needs an event source a hosted thread does not have).
#ifdef KORAOS_HOST_TEST
#define WAIT_PREPARE ""
#define WAIT "yield\n"
#else
#define WAIT_PREPARE "sevl\n"
#define WAIT "wfe\n"
#endif

static void arch_lock(volatile uint32_t *word) {
    uint32_t tmp;
    __asm__ volatile(
        WAIT_PREPARE
        "1: " WAIT
        "2: ldaxr %w0, [%1]\n"
        "   cbnz  %w0, 1b\n"
        "   stxr  %w0, %w2, [%1]\n"
        "   cbnz  %w0, 2b\n"
        : "=&r"(tmp)
        : "r"(word), "r"(1u)
        : "memory");
}

static bool arch_trylock(volatile uint32_t *word) {
    uint32_t tmp;
    uint32_t failed;
    __asm__ volatile(
        "   ldaxr %w0, [%2]\n"
        "   mov   %w1, #1\n"
        "   cbnz  %w0, 1f\n"
        "   stxr  %w1, %w3, [%2]\n"
        "1:\n"
        : "=&r"(tmp), "=&r"(failed)
        : "r"(word), "r"(1u)
        : "memory");
    if (failed) {
        __asm__ volatile("clrex" ::: "memory");
    }
    return failed == 0;
}

static void arch_unlock(volatile uint32_t *word) {
    __asm__ volatile("stlr wzr, [%0]" ::"r"(word) : "memory");
}
#else
// Other hosts (x86 CI runners), or SPINLOCK_PORTABLE host builds, use compiler
// atomics with the same semantics.
static void arch_lock(volatile uint32_t *word) {
    while (__atomic_exchange_n(word, 1, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(word, __ATOMIC_RELAXED)) {
        }
    }
}

static bool arch_trylock(volatile uint32_t *word) {
    return __atomic_exchange_n(word, 1, __ATOMIC_ACQUIRE) == 0;
}

static void arch_unlock(volatile uint32_t *word) {
    __atomic_store_n(word, 0, __ATOMIC_RELEASE);
}
#endif

// Exclusives (LDAXR/STXR) are only architecturally reliable on Normal
// cacheable memory. Before this core's MMU and data cache are on, every access
// is Device-nGnRnE, and on real Cortex-A53/A72 cores the store-exclusive then
// fails forever: a silent hang that QEMU, which emulates exclusives anyway,
// never shows. Refuse loudly instead.
static void check_usable(const struct spinlock *lock, struct cpu *c) {
    if (!c->caches_on && !c->panicking) {
        panic("spinlock '%s' used on cpu %u before its MMU and caches are on", lock->name,
              c->id);
    }
}

static void check_recursion(const struct spinlock *lock, struct cpu *c) {
    check_usable(lock, c);
    if (lock->locked && lock->owner == c->id + 1 && !c->panicking) {
        panic("spinlock '%s' taken twice on cpu %u", lock->name, c->id);
    }
}

void spin_lock(struct spinlock *lock) {
    struct cpu *c = this_cpu();
    check_recursion(lock, c);
    arch_lock(&lock->locked);
    lock->owner = c->id + 1;
    c->locks_held++;
}

bool spin_trylock(struct spinlock *lock) {
    struct cpu *c = this_cpu();
    check_recursion(lock, c);
    if (!arch_trylock(&lock->locked)) {
        return false;
    }
    lock->owner = c->id + 1;
    c->locks_held++;
    return true;
}

void spin_unlock(struct spinlock *lock) {
    struct cpu *c = this_cpu();
    if (!lock->locked || lock->owner != c->id + 1) {
        if (!c->panicking) {
            panic("spinlock '%s' released by cpu %u, which does not hold it", lock->name,
                  c->id);
        }
        return;
    }
    lock->owner = 0;
    c->locks_held--;
    arch_unlock(&lock->locked);
}

bool spin_is_held_here(const struct spinlock *lock) {
    return lock->locked && lock->owner == this_cpu()->id + 1;
}

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    uint64_t flags = irq_save();
    spin_lock(lock);
    return flags;
}

void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    spin_unlock(lock);
    irq_restore(flags);
}
