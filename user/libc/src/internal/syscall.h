// SPDX-License-Identifier: GPL-3.0-or-later
//
// The libc's single doorway into the kernel. __syscall() is implemented in
// src/sys/syscall.S as one `svc #0`; everything else in the libc builds on it
// instead of carrying a stub per call. Host unit tests supply their own
// __syscall() that fakes the kernel (tests/fake_syscall.c).
//
// Arguments and the result are raw machine words; callers cast pointers and
// check the kernel's own error convention (see docs/syscalls.md).
#ifndef LIBC_SYSCALL_H
#define LIBC_SYSCALL_H

#include "syscall_abi.h"

long __syscall(long number, long a0, long a1, long a2, long a3);

static inline long __syscall0(long n) {
    return __syscall(n, 0, 0, 0, 0);
}

static inline long __syscall1(long n, long a0) {
    return __syscall(n, a0, 0, 0, 0);
}

static inline long __syscall2(long n, long a0, long a1) {
    return __syscall(n, a0, a1, 0, 0);
}

static inline long __syscall3(long n, long a0, long a1, long a2) {
    return __syscall(n, a0, a1, a2, 0);
}

static inline long __syscall4(long n, long a0, long a1, long a2, long a3) {
    return __syscall(n, a0, a1, a2, a3);
}

#endif // LIBC_SYSCALL_H
