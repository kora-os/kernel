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

/* Framebuffer geometry, filled by fb_info(). With the flat identity map a user
 * program writes pixels directly to `addr`. */
struct fb_info {
    unsigned long addr;   /* framebuffer base (physical == virtual) */
    unsigned int width;
    unsigned int height;
    unsigned int pitch;   /* bytes per row */
    unsigned int bpp;     /* bits per pixel */
};

/* Console / I/O */
ssize_t write(int fd, const void *buf, size_t len);
ssize_t read(int fd, void *buf, size_t len);

/* Memory */
void *sbrk(long increment);

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
