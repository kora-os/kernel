// KoraOS userland syscall stubs. AAPCS already puts the C arguments in x0..x3,
// so each stub only loads the syscall number into x8 and issues `svc #0`; the
// return value is already in x0 on the way back.

#include "abi.h"

.section .text

.macro SYSCALL name, num
.global \name
\name:
    mov x8, #\num
    svc #0
    ret
.endm

SYSCALL write,   SYS_WRITE
SYSCALL read,    SYS_READ
SYSCALL libk_spawn_flags, SYS_SPAWN_FLAGS  // spawn_flags() in koraos.h flushes stdout first
SYSCALL waitpid, SYS_WAITPID
SYSCALL getpid,  SYS_GETPID
SYSCALL yield,   SYS_YIELD
SYSCALL fb_info, SYS_FB_INFO
SYSCALL open,    SYS_OPEN
SYSCALL close,   SYS_CLOSE
SYSCALL lseek,   SYS_LSEEK
SYSCALL readdir, SYS_READDIR
SYSCALL stat,    SYS_STAT
SYSCALL alloc_pages, SYS_ALLOC_PAGES
SYSCALL free_pages,  SYS_FREE_PAGES
SYSCALL msleep,  SYS_MSLEEP
SYSCALL forbid,  SYS_FORBID
SYSCALL permit,  SYS_PERMIT
SYSCALL task_info, SYS_TASK_INFO
SYSCALL thread_new,  SYS_THREAD_NEW
SYSCALL thread_exit, SYS_THREAD_EXIT
SYSCALL thread_join, SYS_THREAD_JOIN
SYSCALL kill,        SYS_KILL
SYSCALL sysinfo,     SYS_SYSINFO
SYSCALL chdir,   SYS_CHDIR
SYSCALL getcwd,  SYS_GETCWD
SYSCALL volume_info, SYS_VOLUME_INFO
SYSCALL assign,      SYS_ASSIGN
SYSCALL assign_info, SYS_ASSIGN_INFO
SYSCALL sync,        SYS_SYNC
SYSCALL unlink,      SYS_UNLINK
SYSCALL mkdir,       SYS_MKDIR
SYSCALL rmdir,       SYS_RMDIR
SYSCALL port_create, SYS_PORT_CREATE
SYSCALL port_delete, SYS_PORT_DELETE
SYSCALL port_find,   SYS_PORT_FIND
SYSCALL port_put,    SYS_PORT_PUT
SYSCALL port_get,    SYS_PORT_GET
SYSCALL port_wait,   SYS_PORT_WAIT
SYSCALL port_reply,  SYS_PORT_REPLY
SYSCALL std_handles, SYS_STD_HANDLES
SYSCALL libk_spawn_std, SYS_SPAWN_STD  // spawn_std() in koraos.h flushes stdout first
SYSCALL sig_alloc,   SYS_SIG_ALLOC
SYSCALL sig_free,    SYS_SIG_FREE
SYSCALL sig_wait,    SYS_SIG_WAIT
SYSCALL sig_set,     SYS_SIG_SET
SYSCALL sig_send,    SYS_SIG_SEND
SYSCALL port_signal, SYS_PORT_SIGNAL
SYSCALL sig_on_end, SYS_SIG_ON_END
SYSCALL input_claim, SYS_INPUT_CLAIM
SYSCALL input_read, SYS_INPUT_READ
SYSCALL input_focus, SYS_INPUT_FOCUS
SYSCALL fb_claim, SYS_FB_CLAIM

// exit(), rename() and the program entry point come from the libc (user/libc).

// Every thread_create() thread starts here, with x0 = fn and x1 = arg (see
// thread_create in koraos.h): it runs fn(arg) and passes its result to
// thread_exit(), which does not return.
.global libk_thread_start
libk_thread_start:
    mov x2, x0
    mov x0, x1
    blr x2
    mov x8, #SYS_THREAD_EXIT
    svc #0
1:  b 1b
