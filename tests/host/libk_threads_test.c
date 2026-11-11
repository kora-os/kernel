// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for libk under threads: the atomics (koraos.h), the mutex
// (user/libk/mutex.c) and the job API (user/libk/job.c). Built with
// LIBK_HOST_TEST and -pthread: host threads stand in for KoraOS threads
// (thread_new/thread_join are stubbed with pthreads), and libk's allocator
// calls land in counting wrappers over the host's malloc, so every job block
// is shown to be freed. The heap itself is the libc's, tested under threads
// by user/libc/tests/malloc_threads_test.c.

#include "test.h"

typedef unsigned long host_thread_t;
int pthread_create(host_thread_t *thread, const void *attr, void *(*start)(void *), void *arg);
int pthread_join(host_thread_t thread, void **result);
int sched_yield(void);
void *malloc(unsigned long size);
void free(void *ptr);

// The host's allocator, reached before koraos.h renames malloc and free.
static void *host_malloc(unsigned long size) {
    return malloc(size);
}

static void host_free(void *ptr) {
    free(ptr);
}

#include "koraos.h"

// A tiny lock for the stubs themselves (not the code under test).
static int stub_lock;

static void stub_acquire(void) {
    while (__atomic_exchange_n(&stub_lock, 1, __ATOMIC_ACQUIRE)) {
        sched_yield();
    }
}

static void stub_release(void) {
    __atomic_store_n(&stub_lock, 0, __ATOMIC_RELEASE);
}

// libk's allocator calls: the host's, counted -----------------------------

static long blocks_live;

void *libk_malloc(size_t size) {
    void *p = host_malloc(size);
    if (p != NULL) {
        __atomic_fetch_add(&blocks_live, 1, __ATOMIC_RELAXED);
    }
    return p;
}

void libk_free(void *ptr) {
    if (ptr != NULL) {
        __atomic_fetch_sub(&blocks_live, 1, __ATOMIC_RELAXED);
    }
    host_free(ptr);
}

void yield(void) {
    sched_yield();
}

// Thread stubs: a KoraOS thread becomes a host thread ------------------

#define MAX_THREADS 64
#define BAD_CORE 99  // thread_new fails for it, as for a core that does not exist

static struct {
    bool used;
    host_thread_t thread;
    thread_fn fn;
    void *arg;
    long result;
} threads[MAX_THREADS];

static void *thread_body(void *slot) {
    unsigned i = (unsigned)(unsigned long)slot;
    threads[i].result = threads[i].fn(threads[i].arg);
    return NULL;
}

int thread_new(void (*start)(void), thread_fn fn, void *arg, int core) {
    if (start != libk_thread_start || core == BAD_CORE) {
        return -1;
    }
    stub_acquire();
    for (unsigned i = 0; i < MAX_THREADS; i++) {
        if (!threads[i].used) {
            threads[i].used = true;
            threads[i].fn = fn;
            threads[i].arg = arg;
            stub_release();
            if (pthread_create(&threads[i].thread, NULL, thread_body,
                               (void *)(unsigned long)i) != 0) {
                threads[i].used = false;
                return -1;
            }
            return (int)i + 1;  // ids start at 1
        }
    }
    stub_release();
    return -1;
}

int thread_join(int tid, long *value) {
    if (tid < 1 || tid > MAX_THREADS || !threads[tid - 1].used) {
        return -1;
    }
    pthread_join(threads[tid - 1].thread, NULL);
    if (value != NULL) {
        *value = threads[tid - 1].result;
    }
    threads[tid - 1].used = false;
    return 0;
}

void libk_thread_start(void) {}

// Helpers --------------------------------------------------------------

#define WORKERS 8

static void run_workers(void *(*body)(void *)) {
    host_thread_t t[WORKERS];
    for (unsigned i = 0; i < WORKERS; i++) {
        CHECK(pthread_create(&t[i], NULL, body, (void *)(unsigned long)i) == 0, "worker %u", i);
    }
    for (unsigned i = 0; i < WORKERS; i++) {
        pthread_join(t[i], NULL);
    }
}

// Tests ------------------------------------------------------------------

static mutex_t counter_lock = MUTEX_INIT;
static unsigned long counter;  // updated non-atomically, under counter_lock

#define INCREMENTS 50000

static void *count_up(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < INCREMENTS; i++) {
        mutex_lock(&counter_lock);
        unsigned long v = counter;
        __asm__ volatile("" ::: "memory");  // keep the read-modify-write split
        counter = v + 1;
        mutex_unlock(&counter_lock);
    }
    return NULL;
}

static void test_mutex_excludes(void) {
    run_workers(count_up);
    CHECK(counter == (unsigned long)WORKERS * INCREMENTS, "counter %lu, want %lu", counter,
          (unsigned long)WORKERS * INCREMENTS);
    CHECK(mutex_trylock(&counter_lock), "a free mutex was not taken");
    CHECK(!mutex_trylock(&counter_lock), "a held mutex was taken again");
    mutex_unlock(&counter_lock);
}

// Atomics: every worker adds through each operation; any lost update shows
// in the totals.
static long added;
static volatile int cas_count;
static unsigned long swaps;
static int *volatile last_owner;
static int owners[WORKERS];

#define ATOMIC_ROUNDS 100000

static void *hammer_atomics(void *arg) {
    unsigned id = (unsigned)(unsigned long)arg;
    for (unsigned i = 0; i < ATOMIC_ROUNDS; i++) {
        atomic_inc(&added);
        atomic_add(&added, 3);
        atomic_dec(&added);
        int seen;
        do {
            seen = atomic_get(&cas_count);
        } while (!atomic_cas(&cas_count, seen, seen + 1));
        // Swap our own pointer in: whatever came out was someone's (or NULL once).
        int *previous = atomic_swap(&last_owner, &owners[id]);
        if (previous != NULL) {
            atomic_inc(&swaps);
        }
    }
    return NULL;
}

static void test_atomics(void) {
    run_workers(hammer_atomics);
    long rounds = (long)WORKERS * ATOMIC_ROUNDS;
    CHECK(atomic_get(&added) == 3 * rounds, "atomic_add total %ld, want %ld", added, 3 * rounds);
    CHECK(cas_count == rounds, "atomic_cas total %d, want %ld", cas_count, rounds);
    CHECK(swaps == (unsigned long)rounds - 1, "atomic_swap handed over %lu, want %ld", swaps,
          rounds - 1);
    int x = 5;
    CHECK(!atomic_cas(&x, 4, 9) && x == 5, "cas with a stale value changed it");
    CHECK(atomic_cas(&x, 5, 9) && x == 9, "cas with the right value failed");
    CHECK(atomic_add(&x, -10) == -1 && atomic_swap(&x, 7) == -1, "add returns the new value");
    atomic_set(&x, 2);
    CHECK(atomic_get(&x) == 2, "set/get");
}

static long sum_to(void *arg) {
    long n = (long)arg;
    long sum = 0;
    for (long i = 1; i <= n; i++) {
        sum += i;
    }
    return sum;
}

static void test_jobs(void) {
    enum { JOBS = 16 };
    job_t *jobs[JOBS];
    for (long i = 0; i < JOBS; i++) {
        jobs[i] = job_queue(sum_to, (void *)(1000 * (i + 1)), (int)(i % 4));
        CHECK(jobs[i] != NULL, "job %ld was not queued", i);
    }
    for (long i = 0; i < JOBS; i++) {
        long n = 1000 * (i + 1);
        long got = jobs[i] != NULL ? job_result(jobs[i]) : -1;
        CHECK(got == n * (n + 1) / 2, "job %ld returned %ld", i, got);
    }
    CHECK(job_queue(sum_to, (void *)1, BAD_CORE) == NULL, "a job on a bad core was queued");
    CHECK(blocks_live == 0, "jobs left %ld blocks", blocks_live);
}

TEST_MAIN(test_atomics, test_mutex_excludes, test_jobs)
