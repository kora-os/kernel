// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the spinlocks (src/arch/spinlock.c). Each host thread plays
// one core (its own struct cpu via this_cpu()). On an arm64 host the real
// LDAXR/STXR code runs (YIELD in place of WFE); elsewhere the compiler-atomic
// fallback does. Also checks the debug guards: taking a lock twice on one
// core, and releasing a lock another core holds, must panic.

#include "arch/percpu.h"
#include "arch/spinlock.h"
#include "lib/panic.h"
#include "test.h"

// pthread_t is pointer-sized on the hosts we run on (Linux, macOS); declaring
// the calls here avoids system headers, whose types clash with common.h.
typedef unsigned long host_thread_t;
int pthread_create(host_thread_t *thread, const void *attr, void *(*start)(void *), void *arg);
int pthread_join(host_thread_t thread, void **result);
int fork(void);
int waitpid(int pid, int *status, int options);
void _exit(int code) __attribute__((noreturn));

static _Thread_local struct cpu *thread_cpu;
static struct cpu test_cpus[MAX_CPUS];
static _Thread_local int irq_depth;

static bool caches_off;  // simulate a core whose MMU and caches are still off

struct cpu *this_cpu(void) {
    thread_cpu->caches_on = !caches_off;
    return thread_cpu;
}

uint64_t irq_save(void) {
    return (uint64_t)irq_depth++;
}

void irq_restore(uint64_t flags) {
    irq_depth = (int)flags;
}

// Cases that must panic run in a forked child, where panic() exits with
// PANIC_EXIT; anything else (returning, or hanging on the lock) fails.
#define PANIC_EXIT 42

static bool panic_expected;

void panic(const char *fmt, ...) {
    if (!panic_expected) {
        printf("FAIL unexpected panic: %s\n", fmt);
        fflush(NULL);
    }
    _exit(PANIC_EXIT);
}

static bool panics_in_child(void (*scenario)(void)) {
    int pid = fork();
    if (pid == 0) {
        panic_expected = true;
        scenario();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return (status & 0x7f) == 0 && ((status >> 8) & 0xff) == PANIC_EXIT;
}

static struct spinlock guarded = SPINLOCK_INIT("guarded");

static void lock_twice(void) {
    thread_cpu = &test_cpus[0];
    spin_lock(&guarded);
    spin_lock(&guarded);  // would spin forever: must panic instead
}

static void trylock_twice(void) {
    thread_cpu = &test_cpus[0];
    spin_lock(&guarded);
    (void)spin_trylock(&guarded);
}

static void unlock_foreign(void) {
    thread_cpu = &test_cpus[0];
    spin_lock(&guarded);
    test_cpus[1].id = 1;
    thread_cpu = &test_cpus[1];
    spin_unlock(&guarded);  // cpu 1 does not hold it
}

static void test_single_core(void) {
    thread_cpu = &test_cpus[0];
    struct spinlock lock = SPINLOCK_INIT("test");
    spin_lock(&lock);
    CHECK(lock.locked && lock.owner == 1 && spin_is_held_here(&lock), "held by cpu 0");
    CHECK(test_cpus[0].locks_held == 1, "held count");
    spin_unlock(&lock);
    CHECK(!lock.locked && lock.owner == 0 && test_cpus[0].locks_held == 0, "released");

    CHECK(spin_trylock(&lock), "trylock takes a free lock");
    test_cpus[1].id = 1;
    thread_cpu = &test_cpus[1];
    CHECK(!spin_trylock(&lock), "trylock fails while another core holds it");
    thread_cpu = &test_cpus[0];
    spin_unlock(&lock);

    uint64_t flags = spin_lock_irqsave(&lock);
    CHECK(irq_depth == 1, "irqsave masks");
    spin_unlock_irqrestore(&lock, flags);
    CHECK(irq_depth == 0 && test_cpus[0].locks_held == 0, "irqrestore unmasks");
}

static void lock_before_caches(void) {
    thread_cpu = &test_cpus[0];
    caches_off = true;
    spin_lock(&guarded);  // on hardware this would hang in STXR forever
}

static void test_guards(void) {
    CHECK(panics_in_child(lock_before_caches), "spinlock before the MMU and caches panics");
    CHECK(panics_in_child(lock_twice), "recursive spin_lock panics");
    CHECK(panics_in_child(trylock_twice), "recursive spin_trylock panics");
    CHECK(panics_in_child(unlock_foreign), "unlock by a non-holder panics");
}

// Contention: each "core" adds to a plain (non-atomic) counter under the lock.
#define THREADS 4
#define ROUNDS 200000

static struct spinlock counter_lock = SPINLOCK_INIT("counter");
static volatile unsigned long counter;
static volatile int in_critical;
static volatile int overlaps;

static void *contend(void *arg) {
    thread_cpu = arg;
    for (int i = 0; i < ROUNDS; i++) {
        uint64_t flags = spin_lock_irqsave(&counter_lock);
        if (in_critical++ != 0) {
            overlaps++;
        }
        counter = counter + 1;
        in_critical--;
        spin_unlock_irqrestore(&counter_lock, flags);
    }
    return (void *)(unsigned long)thread_cpu->locks_held;
}

static void test_contention(void) {
    host_thread_t threads[THREADS];
    for (unsigned i = 0; i < THREADS; i++) {
        test_cpus[i].id = i;
        test_cpus[i].locks_held = 0;
        CHECK(pthread_create(&threads[i], NULL, contend, &test_cpus[i]) == 0, "thread %u", i);
    }
    for (unsigned i = 0; i < THREADS; i++) {
        void *held = NULL;
        pthread_join(threads[i], &held);
        CHECK(held == NULL, "thread %u ended holding %lu locks", i, (unsigned long)held);
    }
    CHECK(counter == (unsigned long)THREADS * ROUNDS, "counter %lu, want %lu (lost updates)",
          counter, (unsigned long)THREADS * ROUNDS);
    CHECK(overlaps == 0, "%d overlapping critical sections", overlaps);
    CHECK(!counter_lock.locked, "lock free at the end");
}

TEST_MAIN(test_single_core, test_guards, test_contention)
