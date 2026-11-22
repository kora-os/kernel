// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host stand-in for the kernel behind the libc's __syscall(). Tests link this
// instead of src/sys/syscall.S. It implements the few calls the libc code
// under test makes:
//
//   SYS_ALLOC_PAGES / SYS_FREE_PAGES  page runs backed by page-aligned host
//                                     memory, with a page budget to simulate
//                                     exhaustion
//   SYS_YIELD                         sched_yield()
//   SYS_EXIT                          records the status and jumps back to the
//                                     test that armed fake_arm_exit()
//   SYS_WRITE to fd 1 and 2           captured per fd (fake_output), with
//                                     optional short or failing writes
//   SYS_READ from fd 0                scripted input (fake_set_input)
//   SYS_OPEN, CLOSE, READ, WRITE,     an in-memory filesystem of flat paths
//   LSEEK, STAT, MKDIR, UNLINK,       with the kernel's rules: open flag
//   RMDIR, RENAME (fds 3 and up)      checks, no seeking past the end, 16
//                                     fds, read-only files; optional short
//                                     reads and writes and failing writes
//   SYS_SYNC                          counted, with an optional failure
//
// Everything else goes to the handler a test installed (fake_set_handler),
// or fails the way the kernel does for an unknown call (-1).
// Allocation is thread safe so the same stand-in serves the pthread tests.
#ifndef LIBC_FAKE_SYSCALL_H
#define LIBC_FAKE_SYSCALL_H

#include <stddef.h>

#define FAKE_PAGE_SIZE 4096

// Pages currently held by live runs.
size_t fake_pages_live(void);

// Refuse allocations that would take live pages above `pages`; (size_t)-1
// removes the limit.
void fake_set_page_budget(size_t pages);

// Release the freed runs held back for stale reads (see fake_syscall.c).
void fake_flush_freed_runs(void);

// Arm SYS_EXIT: the next exit stores its status and jumps to `env`. An exit
// while unarmed traps. The host's headers are shadowed by the libc's, and
// host_rename.h renames the libc's own setjmp and longjmp, so the host's are
// bound by symbol name here, with a buffer larger than any host's jmp_buf.
typedef struct {
    _Alignas(16) long opaque[64];
} fake_jmp_buf;
#define FAKE_STRINGIFY2(x) #x
#define FAKE_STRINGIFY(x) FAKE_STRINGIFY2(x)
#define FAKE_HOST_SYMBOL(name) __asm__(FAKE_STRINGIFY(__USER_LABEL_PREFIX__) #name)
int fake_setjmp(fake_jmp_buf *env) __attribute__((returns_twice)) FAKE_HOST_SYMBOL(setjmp);
void fake_longjmp(fake_jmp_buf *env, int value) __attribute__((noreturn))
    FAKE_HOST_SYMBOL(longjmp);

void fake_arm_exit(fake_jmp_buf *env);
int fake_exit_status(void);

// The console. fake_output(fd) is what was written to fd 1 or 2 so far, NUL
// terminated (the length in *len if not NULL); fake_writes(fd) counts the
// SYS_WRITE calls. fake_reset_console() clears both, the input and the
// write limits.
const char *fake_output(int fd, size_t *len);
// fd 1 and 2 together, in the order the writes arrived: what the screen shows.
const char *fake_screen(size_t *len);
unsigned fake_writes(int fd);
void fake_reset_console(void);
// Put stdin, stdout and stderr back as a program starts: unread, unbuffered
// state cleared, their handles to be asked for again (SYS_STD_HANDLES). In
// fake_std.c, which a test links when it needs it.
void fake_reset_std_streams(void);
// Each SYS_WRITE takes at most `n` bytes (0: no limit), like a short write.
void fake_set_write_chunk(size_t n);
// While `fail` is set, SYS_WRITE returns -1 and stores nothing.
void fake_fail_writes(int fail);
// fd 0 returns these bytes, at most `chunk` per read (0: all asked for),
// then 0 (end of input).
void fake_set_input(const char *data, size_t len, size_t chunk);
// How many of those bytes fd 0 has handed out so far.
size_t fake_input_position(void);

// The filesystem. fake_put_file creates or replaces a file; fake_file_data
// returns its contents (NULL if it does not exist; length in *len).
// fake_mkdir makes an empty directory entry; fake_set_readonly makes opens
// for writing, unlink and rename of the path fail. fake_open_fds counts open
// descriptors; fake_reset_files removes everything, closes all fds and
// clears the limits and the sync state below.
void fake_put_file(const char *path, const char *data, size_t len);
const char *fake_file_data(const char *path, size_t *len);
void fake_mkdir(const char *path);
void fake_set_readonly(const char *path, int readonly);
int fake_open_fds(void);
void fake_reset_files(void);
// A read or write on a file moves at most this many bytes (0: no limit), as
// a short read or write does.
void fake_set_file_chunks(size_t read_chunk, size_t write_chunk);
// File writes fail (-1) once `bytes` bytes have been written from now on;
// (size_t)-1 removes the limit.
void fake_fail_file_writes_after(size_t bytes);
// SYS_SYNC: fake_syncs counts the calls, which fail while `fail` is set.
unsigned fake_syncs(void);
void fake_fail_sync(int fail);

// Calls the fake kernel does not implement (spawn, waitpid, chdir, ...) go
// to `handler`, with the raw syscall number and arguments; its result is the
// call's. NULL removes it. It runs outside the fake's lock, so it may make
// syscalls of its own (a "child" writing to the console, say).
typedef long (*fake_handler)(long number, long a0, long a1, long a2, long a3);
void fake_set_handler(fake_handler handler);

#endif // LIBC_FAKE_SYSCALL_H
