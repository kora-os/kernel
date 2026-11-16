// SPDX-License-Identifier: GPL-3.0-or-later
//
// signal() and raise() (C11 7.14). Signals are synchronous: raise() calls the
// handler in the caller and returns when the handler does. The kernel
// delivers nothing: its kill() ends a program outright, and a fault at EL0
// halts the machine, so SIGSEGV, SIGILL and SIGFPE only ever come from
// raise().
//
// A handler stays installed after it runs (the standard also allows a reset
// to SIG_DFL), and a signal is not blocked while its handler runs, so a
// handler may raise it again. The default action ends the program without
// flushing its streams, with the negative signal number as the exit code:
// the convention of the kernel's own kill() (-9).

#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include "lock.h"

#define SIGNAL_LIMIT (SIGTERM + 1)

static lock_t lock = LOCK_INIT;
static void (*handlers[SIGNAL_LIMIT])(int);  // all SIG_DFL (a null pointer)

static int known(int sig) {
    return sig == SIGINT || sig == SIGILL || sig == SIGABRT || sig == SIGFPE || sig == SIGSEGV ||
           sig == SIGTERM;
}

void (*signal(int sig, void (*func)(int)))(int) {
    if (!known(sig) || func == SIG_ERR) {
        errno = EINVAL;
        return SIG_ERR;
    }
    __lock(&lock);
    void (*previous)(int) = handlers[sig];
    handlers[sig] = func;
    __unlock(&lock);
    return previous;
}

int raise(int sig) {
    if (!known(sig)) {
        errno = EINVAL;
        return -1;
    }
    __lock(&lock);
    void (*handler)(int) = handlers[sig];
    __unlock(&lock);
    if (handler == SIG_IGN) {
        return 0;
    }
    if (handler == SIG_DFL) {
        _Exit(-sig);
    }
    handler(sig);  // called without the lock: it may call signal() or raise()
    return 0;
}
