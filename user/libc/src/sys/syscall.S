// SPDX-License-Identifier: GPL-3.0-or-later
//
// The libc's one syscall entry: long __syscall(long number, long a0, long a1,
// long a2, long a3). AAPCS passes the number in x0 and the arguments in
// x1..x4; the kernel wants the number in x8 and the arguments in x0..x3, so
// shift everything down and issue `svc #0`. The result is already in x0.

.section .text

.global __syscall
.type __syscall, %function
__syscall:
    mov x8, x0
    mov x0, x1
    mov x1, x2
    mov x2, x3
    mov x3, x4
    svc #0
    ret
.size __syscall, . - __syscall
