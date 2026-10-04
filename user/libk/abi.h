/* KoraOS userland syscall numbers. Shared by the assembly stubs
 * (user/libk/syscall.S) and the C API (user/libk/koraos.h), so it must stay
 * assembler-safe: plain #defines only, no C declarations.
 *
 * These MUST match the kernel side in include/sys/syscall.h.
 *
 * Calling convention: x8 = syscall number, x0..x2 = arguments, `svc #0`,
 * return value in x0.
 */
#pragma once

#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_READ    2
/* 3 was sbrk (retired; returns -1) */
#define SYS_SPAWN   4
#define SYS_WAIT    5
#define SYS_GETPID  6
#define SYS_YIELD   7
#define SYS_FB_INFO 8
#define SYS_OPEN    9
#define SYS_CLOSE   10
#define SYS_LSEEK   11
#define SYS_READDIR 12
#define SYS_STAT    13
#define SYS_ALLOC_PAGES 14
#define SYS_FREE_PAGES  15

#define SYS_CHDIR 32
#define SYS_GETCWD 33
