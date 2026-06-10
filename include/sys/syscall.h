#pragma once

#include "arch/trapframe.h"

// KoraOS private syscall ABI (not POSIX-numbered, but write/exit are shaped
// like the POSIX calls to ease porting later):
//   x8 = syscall number, x0..x2 = args, svc #0, return value in x0.
#define SYS_write 0
#define SYS_exit  1

// Dispatch a syscall described by a trap frame from EL0. The return value is
// written back into the frame's x0.
void syscall_handle(struct trapframe *tf);
