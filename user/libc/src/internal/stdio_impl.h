// SPDX-License-Identifier: GPL-3.0-or-later
//
// The FILE structure and the stdio internals shared by src/stdio/*.c. Every
// public function takes the stream's lock and calls an _unlocked helper, so
// one printf or puts is never interleaved with another thread's output on
// the same stream.
#ifndef LIBC_STDIO_IMPL_H
#define LIBC_STDIO_IMPL_H

#include <stdio.h>

#include "lock.h"

#define F_READ 1u     // opened for reading
#define F_WRITE 2u    // opened for writing
#define F_EOF 4u      // end-of-file indicator
#define F_ERR 8u      // error indicator
#define F_OWNBUF 16u  // buf came from malloc and is freed with the stream
#define F_USERBUF 32u // buf is the caller's (setvbuf): never freed, dropped on change

struct __libc_file {
    int fd;
    unsigned flags;
    int mode;            // _IOFBF, _IOLBF or _IONBF
    unsigned char *buf;  // NULL until the first buffered I/O (or setvbuf)
    size_t size;         // capacity of buf
    size_t wlen;         // bytes waiting in buf to be written
    lock_t lock;
    struct __libc_file *next;  // open streams other than the standard three
};

// exit() calls this, once stdio has been used, to flush every stream.
extern void (*__stdio_exit_hook)(void);
void __stdio_exit(void);

// Writes out the buffered bytes; 0, or EOF with the error indicator set.
int __fflush_unlocked(FILE *f);
// Writes n bytes through the buffer; returns how many were accepted.
size_t __fwrite_unlocked(const void *data, size_t n, FILE *f);
// Makes sure f has its buffer (allocated on first use when it has none).
void __stdio_setup_buffer(FILE *f);

// Streams opened with fopen, for fflush(NULL) and exit.
void __stdio_link(FILE *f);
void __stdio_unlink(FILE *f);
// Calls fn on every stream, the standard ones first.
void __stdio_each(void (*fn)(FILE *));

#endif // LIBC_STDIO_IMPL_H
