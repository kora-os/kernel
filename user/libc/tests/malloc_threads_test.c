// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for the libc heap under real concurrency: several pthreads hammer
// malloc/realloc/free at once, each checking that its own blocks keep their
// contents, then the pools are cross-checked with __malloc_info(). The libc
// lock yields through the fake kernel's SYS_YIELD (tests/fake_syscall.c).
//
// The libc headers shadow the host's, so the few pthread entry points are
// declared by hand. pthread_t is a 64-bit word on the LP64 hosts we run on.

#include "test.h"

#include <stdlib.h>
#include <string.h>

#include "fake_syscall.h"
#include "malloc.h"

typedef unsigned long thread_handle;
int pthread_create(thread_handle *thread, const void *attr, void *(*fn)(void *), void *arg);
int pthread_join(thread_handle thread, void **result);

#define THREADS 4
#define ROUNDS 40000
#define SLOTS 32

struct worker {
    unsigned seed;
    unsigned failures;
};

static void *worker_main(void *arg) {
    struct worker *w = arg;
    unsigned char *p[SLOTS] = {0};
    size_t sz[SLOTS] = {0};
    unsigned char tag[SLOTS] = {0};
    unsigned rng = w->seed;

    for (unsigned r = 0; r < ROUNDS; r++) {
        rng = rng * 1103515245u + 12345u;
        unsigned s = (rng >> 8) % SLOTS;
        if (p[s] != NULL) {
            for (size_t i = 0; i < sz[s]; i++) {
                if (p[s][i] != (unsigned char)(tag[s] + i)) {
                    w->failures++;
                    break;
                }
            }
            if ((rng >> 20) % 4 == 0) {
                size_t grown = sz[s] + (rng >> 12) % 3000;
                unsigned char *q = realloc(p[s], grown);
                if (q == NULL) {
                    w->failures++;
                    continue;
                }
                p[s] = q;
                for (size_t i = sz[s]; i < grown; i++) {
                    p[s][i] = (unsigned char)(tag[s] + i);
                }
                sz[s] = grown;
            } else {
                free(p[s]);
                p[s] = NULL;
            }
            continue;
        }
        rng = rng * 1103515245u + 12345u;
        unsigned kind = (rng >> 8) % 64;
        sz[s] = kind == 0 ? 260000 + (rng >> 12) % 100000  // a direct block
              : kind < 24 ? (rng >> 12) % 128
              : (rng >> 12) % 6000;
        p[s] = malloc(sz[s]);
        if (p[s] == NULL) {
            w->failures++;
            continue;
        }
        tag[s] = (unsigned char)(rng >> 16);
        for (size_t i = 0; i < sz[s]; i++) {
            p[s][i] = (unsigned char)(tag[s] + i);
        }
    }
    for (unsigned s = 0; s < SLOTS; s++) {
        free(p[s]);
    }
    return NULL;
}

static void test_concurrent_heap(void) {
    thread_handle threads[THREADS];
    struct worker workers[THREADS];
    for (unsigned i = 0; i < THREADS; i++) {
        workers[i].seed = 1000 + i * 7919;
        workers[i].failures = 0;
        CHECK(pthread_create(&threads[i], NULL, worker_main, &workers[i]) == 0,
              "pthread_create %u failed", i);
    }
    for (unsigned i = 0; i < THREADS; i++) {
        pthread_join(threads[i], NULL);
        CHECK(workers[i].failures == 0, "thread %u saw %u failures", i, workers[i].failures);
    }

    struct malloc_info info;
    __malloc_info(&info);
    CHECK(info.corrupt == 0, "%zu pool inconsistencies", info.corrupt);
    CHECK(info.used_blocks == 0, "%zu blocks leaked", info.used_blocks);
    CHECK(info.direct_allocs == 0, "%zu direct blocks leaked", info.direct_allocs);
    CHECK(info.bad_frees == 0, "%zu bad frees", info.bad_frees);
    CHECK(info.pools <= 1, "%zu pools kept, want at most 1", info.pools);
    CHECK(fake_pages_live() == info.pool_bytes / MALLOC_PAGE_SIZE,
          "%zu pages held for %zu pool bytes", fake_pages_live(), info.pool_bytes);
}

TEST_MAIN(test_concurrent_heap)
