// SPDX-License-Identifier: GPL-3.0-or-later
//
// See fake_syscall.h. Built with host_rename.h force-included, so the libc's
// free() is kora_free here; the host's own is reached after an #undef.

#include "fake_syscall.h"
#include "syscall.h"

#undef free
int posix_memalign(void **out, size_t align, size_t size);
void free(void *ptr);
int sched_yield(void);

#define MAX_RUNS 4096

static struct {
    void *base;
    size_t pages;
} runs[MAX_RUNS];
static size_t pages_live;
static size_t page_budget = (size_t)-1;
static int state_lock;

static fake_jmp_buf *armed_exit_env;
static int exit_armed;
static int last_exit_status;

// Freed runs stay readable for a while, as on the flat-mapped kernel, so a
// double free of a direct block reads stale (not unmapped) memory.
#define GRAVEYARD 8
static void *graveyard[GRAVEYARD];
static unsigned graveyard_next;

static void lock_state(void) {
    while (__atomic_exchange_n(&state_lock, 1, __ATOMIC_ACQUIRE)) {
        sched_yield();
    }
}

static void unlock_state(void) {
    __atomic_store_n(&state_lock, 0, __ATOMIC_RELEASE);
}

static void bury(void *run) {
    free(graveyard[graveyard_next]);
    graveyard[graveyard_next] = run;
    graveyard_next = (graveyard_next + 1) % GRAVEYARD;
}

static void *alloc_pages_locked(size_t count) {
    if (count == 0 || count > page_budget || pages_live > page_budget - count) {
        return NULL;
    }
    for (unsigned i = 0; i < MAX_RUNS; i++) {
        if (runs[i].base == NULL) {
            void *p = NULL;
            if (posix_memalign(&p, FAKE_PAGE_SIZE, count * FAKE_PAGE_SIZE) != 0) {
                return NULL;
            }
            __builtin_memset(p, 0, count * FAKE_PAGE_SIZE);
            runs[i].base = p;
            runs[i].pages = count;
            pages_live += count;
            return p;
        }
    }
    return NULL;
}

static int free_pages_locked(void *base) {
    for (unsigned i = 0; i < MAX_RUNS; i++) {
        if (runs[i].base != NULL && runs[i].base == base) {
            bury(base);
            runs[i].base = NULL;
            pages_live -= runs[i].pages;
            return 0;
        }
    }
    return -1;
}

#define CONSOLE_CAPACITY (1u << 20)

static struct {
    char data[CONSOLE_CAPACITY];
    size_t len;
    unsigned writes;
} console[3];
static size_t write_chunk;
static int writes_fail;
static const char *input;
static size_t input_len, input_pos, input_chunk;

static long fake_write(int fd, const char *buf, size_t len) {
    if (fd != 1 && fd != 2) {
        return -1;
    }
    lock_state();
    console[fd].writes++;
    if (writes_fail) {
        unlock_state();
        return -1;
    }
    if (write_chunk != 0 && len > write_chunk) {
        len = write_chunk;
    }
    if (len > CONSOLE_CAPACITY - 1 - console[fd].len) {
        len = CONSOLE_CAPACITY - 1 - console[fd].len;
    }
    __builtin_memcpy(console[fd].data + console[fd].len, buf, len);
    console[fd].len += len;
    console[fd].data[console[fd].len] = '\0';
    unlock_state();
    return (long)len;
}

static long fake_read(int fd, char *buf, size_t len) {
    if (fd != 0) {
        return -1;
    }
    lock_state();
    size_t left = input_len - input_pos;
    size_t n = len < left ? len : left;
    if (input_chunk != 0 && n > input_chunk) {
        n = input_chunk;
    }
    __builtin_memcpy(buf, input + input_pos, n);
    input_pos += n;
    unlock_state();
    return (long)n;
}

const char *fake_output(int fd, size_t *len) {
    if (len != NULL) {
        *len = console[fd].len;
    }
    return console[fd].data;
}

unsigned fake_writes(int fd) {
    return console[fd].writes;
}

void fake_reset_console(void) {
    lock_state();
    for (int fd = 0; fd < 3; fd++) {
        console[fd].len = 0;
        console[fd].data[0] = '\0';
        console[fd].writes = 0;
    }
    write_chunk = 0;
    writes_fail = 0;
    input = NULL;
    input_len = input_pos = input_chunk = 0;
    unlock_state();
}

void fake_set_write_chunk(size_t n) {
    write_chunk = n;
}

void fake_fail_writes(int fail) {
    writes_fail = fail;
}

void fake_set_input(const char *data, size_t len, size_t chunk) {
    lock_state();
    input = data;
    input_len = len;
    input_pos = 0;
    input_chunk = chunk;
    unlock_state();
}

long __syscall(long number, long a0, long a1, long a2, long a3) {
    (void)a3;
    switch (number) {
    case SYS_WRITE:
        return fake_write((int)a0, (const char *)a1, (size_t)a2);
    case SYS_READ:
        return fake_read((int)a0, (char *)a1, (size_t)a2);
    case SYS_ALLOC_PAGES: {
        lock_state();
        void *p = alloc_pages_locked((size_t)a0);
        unlock_state();
        return (long)p;
    }
    case SYS_FREE_PAGES: {
        lock_state();
        int r = free_pages_locked((void *)a0);
        unlock_state();
        return r;
    }
    case SYS_YIELD:
        sched_yield();
        return 0;
    case SYS_EXIT:
        if (!exit_armed) {
            __builtin_trap();
        }
        exit_armed = 0;
        last_exit_status = (int)a0;
        longjmp(armed_exit_env, 1);
    default:
        return -1;
    }
}

size_t fake_pages_live(void) {
    lock_state();
    size_t n = pages_live;
    unlock_state();
    return n;
}

void fake_set_page_budget(size_t pages) {
    lock_state();
    page_budget = pages;
    unlock_state();
}

void fake_flush_freed_runs(void) {
    lock_state();
    for (unsigned i = 0; i < GRAVEYARD; i++) {
        bury(NULL);
    }
    unlock_state();
}

void fake_arm_exit(fake_jmp_buf *env) {
    armed_exit_env = env;
    exit_armed = 1;
}

int fake_exit_status(void) {
    return last_exit_status;
}
