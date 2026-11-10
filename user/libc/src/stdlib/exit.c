// SPDX-License-Identifier: GPL-3.0-or-later
//
// Program termination. There is no atexit() yet, so exit() ends the program
// right away, like _Exit(). The kernel ends the whole program, from any of its
// threads, and reclaims its pages.

#include <stdlib.h>

#include "syscall.h"

// Exit code of a program ended by abort(): negative signal number, like the
// kernel's own kill() (-9).
#define EXIT_ABORTED (-6)

void _Exit(int status) {
    __syscall1(SYS_EXIT, status);
    for (;;) {
        // The kernel never returns from SYS_EXIT; spin rather than run off
        // into whatever follows if it ever did.
    }
}

void exit(int status) {
    _Exit(status);
}

void abort(void) {
    _Exit(EXIT_ABORTED);
}
