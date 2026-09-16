#pragma once

#include "arch/trapframe.h"

// KoraOS private syscall ABI (not POSIX-numbered, but the calls are shaped like
// the POSIX ones to ease porting later):
//   x8 = syscall number, x0..x2 = args, svc #0, return value in x0.
// These numbers must match the userland side in user/libk/abi.h. Not all are
// serviced yet; unimplemented numbers return -1.
#define SYS_write  0
#define SYS_exit   1
#define SYS_read   2
#define SYS_sbrk   3
#define SYS_spawn  4
#define SYS_wait   5
#define SYS_getpid 6
#define SYS_yield  7
#define SYS_fb_info 8
#define SYS_open    9
#define SYS_close   10
#define SYS_lseek   11
#define SYS_readdir 12
#define SYS_stat    13

// Dispatch a syscall described by a trap frame from EL0. The return value is
// written back into the frame's x0.
void syscall_handle(struct trapframe *tf);
