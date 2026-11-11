// SPDX-License-Identifier: GPL-3.0-or-later
//
// See fake_syscall.h. Built with host_rename.h force-included, so the libc's
// free() is kora_free here; the host's own is reached after an #undef.

#include "fake_syscall.h"
#include "syscall.h"

#undef free
#undef malloc
int posix_memalign(void **out, size_t align, size_t size);
void *malloc(size_t size);
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

// The filesystem: a few flat entries, contents in host memory.
#define MAX_FILES 32
#define MAX_FDS 16
#define FD_BASE 3

static struct fake_file {
    char path[128];
    char *data;
    size_t len, cap;
    int is_dir, readonly, used;
} files[MAX_FILES];

static struct {
    int file;  // index into files, -1 when free
    size_t pos;
    int flags;
} fds[MAX_FDS] = {[0 ... MAX_FDS - 1] = {-1, 0, 0}};

static int find_file(const char *path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (files[i].used && __builtin_strcmp(files[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

static int new_file(const char *path) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!files[i].used) {
            __builtin_memset(&files[i], 0, sizeof files[i]);
            files[i].used = 1;
            __builtin_strncpy(files[i].path, path, sizeof files[i].path - 1);
            return i;
        }
    }
    return -1;
}

static void free_data(struct fake_file *f) {
    free(f->data);
    f->data = NULL;
    f->len = f->cap = 0;
}

static int file_in_use(int file) {
    for (int i = 0; i < MAX_FDS; i++) {
        if (fds[i].file == file) {
            return 1;
        }
    }
    return 0;
}

#define K_ACCESS 3
#define K_CREAT 0x100
#define K_TRUNC 0x200
#define K_APPEND 0x400
#define K_EXCL 0x800

static long fs_open(const char *path, int flags) {
    int access = flags & K_ACCESS;
    if ((flags & ~(K_ACCESS | K_CREAT | K_TRUNC | K_APPEND | K_EXCL)) || access == 3 ||
        (access == 0 && (flags & (K_TRUNC | K_APPEND))) || ((flags & K_EXCL) && !(flags & K_CREAT))) {
        return -1;  // the kernel's own checks
    }
    int fd = -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (fds[i].file < 0) {
            fd = i;
            break;
        }
    }
    if (fd < 0) {
        return -1;
    }
    int file = find_file(path);
    if (file >= 0 && (flags & K_EXCL)) {
        return -1;
    }
    if (file >= 0 && files[file].is_dir && access != 0) {
        return -1;
    }
    if (file >= 0 && files[file].readonly && access != 0) {
        return -1;
    }
    if (file < 0) {
        if (!(flags & K_CREAT) || (file = new_file(path)) < 0) {
            return -1;
        }
    } else if (flags & K_TRUNC) {
        files[file].len = 0;
    }
    fds[fd].file = file;
    fds[fd].pos = 0;
    fds[fd].flags = flags;
    return fd + FD_BASE;
}

static int fd_index(long fd) {
    long i = fd - FD_BASE;
    return i >= 0 && i < MAX_FDS && fds[i].file >= 0 ? (int)i : -1;
}

static long fs_read(int i, char *buf, size_t len) {
    struct fake_file *f = &files[fds[i].file];
    if ((fds[i].flags & K_ACCESS) == 1 || f->is_dir) {
        return -1;
    }
    size_t left = f->len - fds[i].pos;
    size_t n = len < left ? len : left;
    if (n == 0) {
        return 0;  // an empty file may have no data at all (NULL + 0 is UB)
    }
    __builtin_memcpy(buf, f->data + fds[i].pos, n);
    fds[i].pos += n;
    return (long)n;
}

static long fs_write(int i, const char *buf, size_t len) {
    struct fake_file *f = &files[fds[i].file];
    if ((fds[i].flags & K_ACCESS) == 0) {
        return -1;
    }
    if (fds[i].flags & K_APPEND) {
        fds[i].pos = f->len;
    }
    if (len == 0) {
        return 0;
    }
    size_t end = fds[i].pos + len;
    if (end > f->cap) {
        size_t cap = f->cap ? f->cap : 64;
        while (cap < end) {
            cap *= 2;
        }
        char *grown = malloc(cap);
        if (grown == NULL) {
            return -1;
        }
        if (f->data != NULL) {
            __builtin_memcpy(grown, f->data, f->len);
        }
        free(f->data);
        f->data = grown;
        f->cap = cap;
    }
    __builtin_memcpy(f->data + fds[i].pos, buf, len);
    fds[i].pos = end;
    if (end > f->len) {
        f->len = end;
    }
    return (long)len;
}

static long fs_lseek(int i, long offset, int whence) {
    long base = whence == 0 ? 0 : whence == 1 ? (long)fds[i].pos : whence == 2 ? (long)files[fds[i].file].len : -1;
    if (base < 0 || offset < -base || offset > (long)files[fds[i].file].len - base) {
        return -1;  // the kernel cannot seek before the start or past the end
    }
    fds[i].pos = (size_t)(base + offset);
    return (long)fds[i].pos;
}

struct fake_stat {
    unsigned long size;
    int is_dir;
};

static long fs_syscall(long number, long a0, long a1, long a2) {
    int i;
    switch (number) {
    case SYS_OPEN:
        return fs_open((const char *)a0, (int)a1);
    case SYS_CLOSE:
        if ((i = fd_index(a0)) < 0) {
            return -1;
        }
        fds[i].file = -1;
        return 0;
    case SYS_READ:
        return (i = fd_index(a0)) < 0 ? -1 : fs_read(i, (char *)a1, (size_t)a2);
    case SYS_WRITE:
        return (i = fd_index(a0)) < 0 ? -1 : fs_write(i, (const char *)a1, (size_t)a2);
    case SYS_LSEEK:
        return (i = fd_index(a0)) < 0 ? -1 : fs_lseek(i, a1, (int)a2);
    case SYS_STAT: {
        int f = find_file((const char *)a0);
        if (f < 0) {
            return -1;
        }
        struct fake_stat *st = (struct fake_stat *)a1;
        st->size = files[f].len;
        st->is_dir = files[f].is_dir;
        return 0;
    }
    case SYS_UNLINK:
    case SYS_RMDIR: {
        int f = find_file((const char *)a0);
        if (f < 0 || files[f].readonly || files[f].is_dir != (number == SYS_RMDIR) ||
            file_in_use(f)) {
            return -1;
        }
        free_data(&files[f]);
        files[f].used = 0;
        return 0;
    }
    case SYS_RENAME: {
        int f = find_file((const char *)a0);
        if (f < 0 || files[f].readonly || find_file((const char *)a1) >= 0) {
            return -1;
        }
        __builtin_memset(files[f].path, 0, sizeof files[f].path);
        __builtin_strncpy(files[f].path, (const char *)a1, sizeof files[f].path - 1);
        return 0;
    }
    default:
        return -1;
    }
}

void fake_put_file(const char *path, const char *data, size_t len) {
    lock_state();
    int f = find_file(path);
    if (f < 0) {
        f = new_file(path);
    }
    free_data(&files[f]);
    files[f].data = malloc(len ? len : 1);
    __builtin_memcpy(files[f].data, data, len);
    files[f].len = files[f].cap = len;
    unlock_state();
}

const char *fake_file_data(const char *path, size_t *len) {
    int f = find_file(path);
    if (f < 0) {
        return NULL;
    }
    if (len != NULL) {
        *len = files[f].len;
    }
    return files[f].data != NULL ? files[f].data : "";
}

void fake_mkdir(const char *path) {
    lock_state();
    int f = new_file(path);
    files[f].is_dir = 1;
    unlock_state();
}

void fake_set_readonly(const char *path, int readonly) {
    int f = find_file(path);
    if (f >= 0) {
        files[f].readonly = readonly;
    }
}

int fake_open_fds(void) {
    int n = 0;
    for (int i = 0; i < MAX_FDS; i++) {
        n += fds[i].file >= 0;
    }
    return n;
}

void fake_reset_files(void) {
    lock_state();
    for (int i = 0; i < MAX_FDS; i++) {
        fds[i].file = -1;
    }
    for (int f = 0; f < MAX_FILES; f++) {
        if (files[f].used) {
            free_data(&files[f]);
            files[f].used = 0;
        }
    }
    unlock_state();
}

long __syscall(long number, long a0, long a1, long a2, long a3) {
    (void)a3;
    if (a0 >= FD_BASE && (number == SYS_READ || number == SYS_WRITE)) {
        lock_state();
        long r = fs_syscall(number, a0, a1, a2);
        unlock_state();
        return r;
    }
    switch (number) {
    case SYS_OPEN:
    case SYS_CLOSE:
    case SYS_LSEEK:
    case SYS_STAT:
    case SYS_UNLINK:
    case SYS_RMDIR:
    case SYS_RENAME: {
        lock_state();
        long r = fs_syscall(number, a0, a1, a2);
        unlock_state();
        return r;
    }
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
