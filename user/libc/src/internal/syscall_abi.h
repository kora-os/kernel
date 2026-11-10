/* KoraOS syscall numbers as the libc sees them. Shared by the assembly entry
 * (src/sys/syscall.S) and the C code, so it must stay assembler-safe: plain
 * #defines only, no C declarations.
 *
 * This is libc's own copy of the table (libk keeps another in user/libk/abi.h
 * until the two merge). Both MUST match the kernel side in
 * include/sys/syscall.h; tests/check-syscall-abi.py enforces that.
 *
 * Calling convention: x8 = syscall number, x0..x3 = arguments, `svc #0`,
 * return value in x0.
 */
#ifndef LIBC_SYSCALL_ABI_H
#define LIBC_SYSCALL_ABI_H

#define SYS_WRITE   0
#define SYS_EXIT    1
#define SYS_READ    2
/* 3 was sbrk (retired; returns -1) */
#define SYS_SPAWN_FLAGS 4
#define SYS_WAITPID 5
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
#define SYS_MSLEEP      16
#define SYS_FORBID      17
#define SYS_PERMIT      18
#define SYS_TASK_INFO   19
#define SYS_THREAD_NEW  20
#define SYS_THREAD_EXIT 21
#define SYS_THREAD_JOIN 22
#define SYS_KILL        23
#define SYS_SYSINFO     24

#define SYS_CHDIR 32
#define SYS_GETCWD 33
#define SYS_VOLUME_INFO 34
#define SYS_ASSIGN      35
#define SYS_ASSIGN_INFO 36
#define SYS_SYNC        37
#define SYS_UNLINK      38
#define SYS_MKDIR       39
#define SYS_RMDIR       40
#define SYS_RENAME      41

#endif /* LIBC_SYSCALL_ABI_H */
