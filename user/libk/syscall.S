// KoraOS userland syscall stubs. AAPCS already puts the C arguments in x0..x2,
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
SYSCALL sbrk,    SYS_SBRK
SYSCALL spawn,   SYS_SPAWN
SYSCALL wait,    SYS_WAIT
SYSCALL getpid,  SYS_GETPID
SYSCALL yield,   SYS_YIELD
SYSCALL fb_info, SYS_FB_INFO

// exit() never returns; if the kernel ever did return here, spin rather than
// running off into whatever follows.
.global exit
exit:
    mov x8, #SYS_EXIT
    svc #0
1:  b 1b
