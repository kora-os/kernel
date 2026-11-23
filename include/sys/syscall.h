#pragma once

#include "arch/trapframe.h"

// KoraOS private syscall ABI (not POSIX-numbered, but the calls are shaped like
// the POSIX ones to ease porting later):
//   x8 = syscall number, x0..x3 = args, svc #0, return value in x0.
// These numbers must match the userland side in user/libk/abi.h. Not all are
// serviced yet; unimplemented numbers return -1.
#define SYS_write  0
#define SYS_exit   1
#define SYS_read   2
// 3 was sbrk (retired; returns -1)
#define SYS_spawn_flags 4
#define SYS_waitpid 5
#define SYS_getpid 6
#define SYS_yield  7
#define SYS_fb_info 8
#define SYS_open    9
#define SYS_close   10
#define SYS_lseek   11
#define SYS_readdir 12
#define SYS_stat    13
#define SYS_alloc_pages 14
#define SYS_free_pages  15
#define SYS_msleep      16
#define SYS_forbid      17
#define SYS_permit      18
#define SYS_task_info   19
#define SYS_thread_new  20
#define SYS_thread_exit 21
#define SYS_thread_join 22
#define SYS_kill        23
#define SYS_sysinfo     24
// Slots 25 through 31 are reserved for task and memory work.
#define SYS_chdir   32
#define SYS_getcwd  33
#define SYS_volume_info 34
#define SYS_assign      35
#define SYS_assign_info 36
#define SYS_sync        37
#define SYS_unlink      38
#define SYS_mkdir       39
#define SYS_rmdir       40
#define SYS_rename      41
// Message ports (ipc/port.h).
#define SYS_port_create  48
#define SYS_port_delete  49
#define SYS_port_find    50
#define SYS_port_put     51
#define SYS_port_get     52
#define SYS_port_wait    53
#define SYS_port_reply   54
// Standard handles (proc/task.h).
#define SYS_std_handles  55
#define SYS_spawn_std    56
// Signals (proc/task.h).
#define SYS_sig_alloc    57
#define SYS_sig_free     58
#define SYS_sig_wait     59
#define SYS_sig_set      60
#define SYS_sig_send     61
#define SYS_port_signal  62

#define SYS_sig_on_end   63
#define SYS_input_claim  64
#define SYS_input_read   65
#define SYS_input_focus  66
#define SYS_fb_claim     67
#define SYS_gettid       68
#define SYS_conlog       69

// Dispatch a syscall described by a trap frame from EL0. The return value is
// written back into the frame's x0.
void syscall_handle(struct trapframe *tf);
