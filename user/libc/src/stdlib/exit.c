// SPDX-License-Identifier: GPL-3.0-or-later
//
// Program termination (C11 7.22.4). exit() runs the atexit() handlers, last
// registered first, then ends the program; quick_exit() does the same with
// the at_quick_exit() handlers; _Exit() and abort() end it at once. The
// kernel ends the whole program, from any of its threads, and reclaims its
// pages. A handler may register more handlers; they run too.

#include <stdlib.h>

#include "lock.h"
#include "syscall.h"

// Exit code of a program ended by abort(): negative signal number, like the
// kernel's own kill() (-9).
#define EXIT_ABORTED (-6)

// The standard asks for at least 32 of each.
#define MAX_HANDLERS 32

struct handlers {
    lock_t lock;
    int count;
    void (*fn[MAX_HANDLERS])(void);
};

static struct handlers at_exit = {LOCK_INIT, 0, {0}};
static struct handlers at_quick = {LOCK_INIT, 0, {0}};

static int add_handler(struct handlers *h, void (*func)(void)) {
    int ok = 0;
    __lock(&h->lock);
    if (h->count < MAX_HANDLERS) {
        h->fn[h->count++] = func;
        ok = 1;
    }
    __unlock(&h->lock);
    return ok ? 0 : -1;
}

// Pops one handler at a time, so the lock is not held while it runs (it may
// register another).
static void run_handlers(struct handlers *h) {
    for (;;) {
        void (*fn)(void) = NULL;
        __lock(&h->lock);
        if (h->count > 0) {
            fn = h->fn[--h->count];
        }
        __unlock(&h->lock);
        if (fn == NULL) {
            return;
        }
        fn();
    }
}

int atexit(void (*func)(void)) {
    return add_handler(&at_exit, func);
}

int at_quick_exit(void (*func)(void)) {
    return add_handler(&at_quick, func);
}

void _Exit(int status) {
    __syscall1(SYS_EXIT, status);
    for (;;) {
        // The kernel never returns from SYS_EXIT; spin rather than run off
        // into whatever follows if it ever did.
    }
}

void exit(int status) {
    run_handlers(&at_exit);
    _Exit(status);
}

void quick_exit(int status) {
    run_handlers(&at_quick);
    _Exit(status);
}

void abort(void) {
    _Exit(EXIT_ABORTED);
}
