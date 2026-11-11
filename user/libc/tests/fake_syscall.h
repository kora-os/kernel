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
//   LSEEK, STAT, UNLINK, RMDIR,       with the kernel's rules: open flag
//   RENAME (fds 3 and up)             checks, no seeking past the end, 16
//                                     fds, read-only files
//
// Everything else fails the way the kernel does for an unknown call (-1).
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

// Arm SYS_EXIT: the next exit stores its status and longjmp()s to `env`. An
// exit while unarmed traps. The host's headers are shadowed by the libc's, so
// setjmp/longjmp are declared by hand with a buffer larger than any host's
// jmp_buf.
typedef struct {
    _Alignas(16) long opaque[64];
} fake_jmp_buf;
int setjmp(fake_jmp_buf *env) __attribute__((returns_twice));
void longjmp(fake_jmp_buf *env, int value) __attribute__((noreturn));

void fake_arm_exit(fake_jmp_buf *env);
int fake_exit_status(void);

// The console. fake_output(fd) is what was written to fd 1 or 2 so far, NUL
// terminated (the length in *len if not NULL); fake_writes(fd) counts the
// SYS_WRITE calls. fake_reset_console() clears both, the input and the
// write limits.
const char *fake_output(int fd, size_t *len);
unsigned fake_writes(int fd);
void fake_reset_console(void);
// Each SYS_WRITE takes at most `n` bytes (0: no limit), like a short write.
void fake_set_write_chunk(size_t n);
// While `fail` is set, SYS_WRITE returns -1 and stores nothing.
void fake_fail_writes(int fail);
// fd 0 returns these bytes, at most `chunk` per read (0: all asked for),
// then 0 (end of input).
void fake_set_input(const char *data, size_t len, size_t chunk);

// The filesystem. fake_put_file creates or replaces a file; fake_file_data
// returns its contents (NULL if it does not exist; length in *len).
// fake_mkdir makes an empty directory entry; fake_set_readonly makes opens
// for writing, unlink and rename of the path fail. fake_open_fds counts open
// descriptors; fake_reset_files removes everything and closes all fds.
void fake_put_file(const char *path, const char *data, size_t len);
const char *fake_file_data(const char *path, size_t *len);
void fake_mkdir(const char *path);
void fake_set_readonly(const char *path, int readonly);
int fake_open_fds(void);
void fake_reset_files(void);

#endif // LIBC_FAKE_SYSCALL_H
