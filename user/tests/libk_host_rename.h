// SPDX-License-Identifier: GPL-3.0-or-later
//
// Force-included (-include) when user programs and libk are built with the
// host compiler for the tests in this directory. libk's syscall stubs are
// renamed, so the code under test reaches libk_host.c (and through it the
// fake kernel) and never a host function of the same name: read(), open()
// and kill(), but also sysinfo() on Linux or task_info() on macOS. The libc
// is renamed the same way by user/libc/tests/host_rename.h.
#ifndef LIBK_HOST_RENAME_H
#define LIBK_HOST_RENAME_H

#define write libk_write
#define read libk_read
#define waitpid libk_waitpid
#define getpid libk_getpid
#define yield libk_yield
#define fb_info libk_fb_info
#define open libk_open
#define close libk_close
#define lseek libk_lseek
#define readdir libk_readdir
#define stat libk_stat
#define alloc_pages libk_alloc_pages
#define free_pages libk_free_pages
#define msleep libk_msleep
#define forbid libk_forbid
#define permit libk_permit
#define task_info libk_task_info
#define thread_new libk_thread_new
#define thread_exit libk_thread_exit
#define thread_join libk_thread_join
#define kill libk_kill
#define sysinfo libk_sysinfo
#define chdir libk_chdir
#define getcwd libk_getcwd
#define volume_info libk_volume_info
#define assign libk_assign
#define assign_info libk_assign_info
#define sync libk_sync
#define unlink libk_unlink
#define mkdir libk_mkdir
#define rmdir libk_rmdir
#define wait libk_wait

#endif // LIBK_HOST_RENAME_H
