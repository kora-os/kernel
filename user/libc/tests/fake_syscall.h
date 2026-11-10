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

#endif // LIBC_FAKE_SYSCALL_H
