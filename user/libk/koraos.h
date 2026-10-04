/* KoraOS userland C API. Freestanding: no libc, no kernel headers -- everything
 * a user program needs is declared here. Links against user/libk/syscall.S.
 *
 * The calls are shaped like their POSIX namesakes so a fuller libc shim can wrap
 * them later, but the numbering is KoraOS-private (see abi.h). Not every call is
 * serviced by the kernel yet; unimplemented ones currently return -1.
 */
#pragma once

#include "abi.h"

typedef unsigned long size_t;
typedef long ssize_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

/* Framebuffer geometry, filled by fb_info(). With the flat identity map a user
 * program writes pixels directly to `addr`. */
struct fb_info {
    unsigned long addr;   /* framebuffer base (physical == virtual) */
    unsigned int width;
    unsigned int height;
    unsigned int pitch;   /* bytes per row */
    unsigned int bpp;     /* bits per pixel */
};

/* Filesystem. Paths are absolute; names are UTF-8. The kernel mirrors these
 * struct layouts in src/sys/syscall.c -- keep them in sync. */
#define O_RDONLY 0

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* Max name length in UTF-8 bytes incl. NUL (255 UTF-16 units * 3); must match
 * the kernel's FAT32_NAME_MAX + 1. */
#define DIRENT_NAME_MAX 766

struct dirent {
    unsigned long size;
    int is_dir;
    char name[DIRENT_NAME_MAX];
};

struct stat {
    unsigned long size;
    int is_dir;
};

/* Console / I/O */
ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);

/* Files: open() takes O_RDONLY (files and directories); read() serves file fds,
 * readdir() serves directory fds. */
int open(const char *path, int flags);
int close(int fd);
long lseek(int fd, long offset, int whence);
int readdir(int fd, struct dirent *out);   /* 1 = entry, 0 = end, <0 = error */
int stat(const char *path, struct stat *out);

// Change cwd, or copy its volume-qualified canonical spelling (including NUL).
// Both return 0 on success and -1 on failure; failed chdir preserves cwd.
int chdir(const char *path);
int getcwd(char *buf, size_t size);

/* Memory. alloc_pages() returns `count` contiguous zeroed pages (NULL on
 * failure); free_pages() takes the base address back (0, or -1 if the run is
 * not yours). Pages a program does not free are reclaimed when it is reaped.
 * malloc() and friends (user/libk/malloc.c) build a heap on top: 16-byte
 * aligned blocks from pools of pages, so the heap grows as long as the system
 * has free pages. */
#define KORAOS_PAGE_SIZE 4096

#ifdef LIBK_HOST_TEST
/* Host unit tests link libk's malloc next to the host libc: rename it so it
 * does not replace the host's own allocator. */
#define malloc libk_malloc
#define free libk_free
#define calloc libk_calloc
#define realloc libk_realloc
#endif

void *alloc_pages(size_t count);
int free_pages(void *base);

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t count, size_t size);
void *realloc(void *ptr, size_t size);

/* Heap diagnostics, walked from the pools (for tests and curious programs). */
struct kheap_info {
    size_t pools;          /* page-run pools backing malloc */
    size_t pool_bytes;     /* their total size */
    size_t used_blocks;    /* live malloc blocks in the pools */
    size_t used_bytes;     /* their size, headers included */
    size_t free_bytes;     /* free space in the pools */
    size_t direct_allocs;  /* large blocks with page runs of their own */
    size_t direct_bytes;
    size_t bad_frees;      /* misaligned or double frees ignored */
    size_t corrupt;        /* inconsistencies found in the pools (should be 0) */
};
void kheap_info(struct kheap_info *out);

/* Memory primitives (user/libk/mem.c); the compiler may also emit calls. */
void *memset(void *dest, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);
void *memmove(void *dest, const void *src, size_t n);

/* Process control */
void exit(int status) __attribute__((noreturn));
int spawn(const char *name, int argc, char *const argv[]);
int wait(int pid);
int getpid(void);
void yield(void);

/* Graphics */
int fb_info(struct fb_info *out);

/* Small freestanding conveniences shared by the demo programs. */
static inline size_t kstrlen(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static inline int kstreq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static inline void kputs(const char *s) {
    write(1, s, kstrlen(s));
}

static inline void kput_int(long v) {
    if (v < 0) {
        write(1, "-", 1);
        v = -v;
    }
    unsigned long u = (unsigned long)v;
    if (u == 0) {
        write(1, "0", 1);
        return;
    }
    char buf[24];
    int i = 0;
    while (u > 0) {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    }
    char out[24];
    int j = 0;
    while (i > 0) {
        out[j++] = buf[--i];
    }
    write(1, out, (size_t)j);
}
