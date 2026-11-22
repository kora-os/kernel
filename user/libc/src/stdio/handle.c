// SPDX-License-Identifier: GPL-3.0-or-later
//
// The standard streams on the program's standard handles (KoraOS): each of
// stdin, stdout and stderr is the kernel console, an open file, or a console
// handler's message port (_libc/console.h), as the parent chose at spawn. The
// libc asks the kernel once, on the first I/O, and then reads and writes each
// one its own way: the console and files through read and write, a handler
// with packets, as AmigaOS's DOS packets do.

#include <_libc/console.h>
#include <errno.h>

#include "stdio_impl.h"
#include "syscall.h"

// Mirrors the kernel's PORT_REPLY and struct port_received (ipc/port.h).
#define PORT_REPLY 2
struct port_received {
    void *msg;
    int kind;
    int sender;
};

static int std_kind[3];  // what __stdio_std_init learnt
int __stdio_std_ready;
static lock_t std_lock = LOCK_INIT;

void __stdio_std_init(void) {
    if (__atomic_load_n(&__stdio_std_ready, __ATOMIC_ACQUIRE)) {
        return;
    }
    __lock(&std_lock);
    if (!__stdio_std_ready) {
        struct std_handle handles[3] = {{STD_CONSOLE, 0}, {STD_CONSOLE, 0}, {STD_CONSOLE, 0}};
        // A kernel without standard handles gives the console for all three.
        if (__syscall1(SYS_STD_HANDLES, (long)handles) != 0) {
            for (int i = 0; i < 3; i++) {
                handles[i] = (struct std_handle){STD_CONSOLE, 0};
            }
        }
        for (int i = 0; i < 3; i++) {
            FILE *f = __stdio_standard(i);
            std_kind[i] = handles[i].kind;
            if (handles[i].kind == STD_FILE) {
                f->fd = handles[i].value;
                if (f->mode == _IOLBF) {
                    f->mode = _IOFBF;  // not a console: no need to flush per line
                }
            } else if (handles[i].kind == STD_PORT) {
                f->port = handles[i].value;
            }
        }
        __atomic_store_n(&__stdio_std_ready, 1, __ATOMIC_RELEASE);
    }
    __unlock(&std_lock);
}

// One packet to the stream's console handler, and its answer.
static long transact(FILE *f, int op, void *buf, size_t n) {
    if (f->reply == 0) {
        long port = __syscall1(SYS_PORT_CREATE, 0);
        if (port <= 0) {
            return -1;
        }
        f->reply = (int)port;
    }
    struct con_packet packet = {op, -1, buf, n};
    if (__syscall3(SYS_PORT_PUT, f->port, (long)&packet, f->reply) != 0) {
        return -1;  // the handler is gone
    }
    struct port_received got;
    // Only this stream's packets come back here, one at a time (its lock is
    // held): the next message is this packet, replied or returned unhandled.
    if (__syscall2(SYS_PORT_WAIT, f->reply, (long)&got) != 1 || got.msg != &packet ||
        got.kind != PORT_REPLY) {
        return -1;
    }
    return packet.result;
}

long __stdio_read_raw(FILE *f, void *buf, size_t n) {
    __stdio_std_init();
    if (f->port != 0) {
        return transact(f, CON_READ, buf, n);
    }
    return __syscall3(SYS_READ, f->fd, (long)buf, (long)n);
}

long __stdio_write_raw(FILE *f, const void *buf, size_t n) {
    __stdio_std_init();
    if (f->port != 0) {
        return transact(f, CON_WRITE, (void *)buf, n);
    }
    return __syscall3(SYS_WRITE, f->fd, (long)buf, (long)n);
}

int __con_mode(int mode) {
    __stdio_std_init();
    if (stdin->port == 0) {
        return -1;
    }
    __lock(&stdin->lock);
    long result = transact(stdin, CON_MODE, NULL, (size_t)mode);
    __unlock(&stdin->lock);
    return result == 0 ? 0 : -1;
}

// POSIX isatty for the standard streams' numbers: whether stdin (0), stdout
// (1) or stderr (2) is a console, the kernel's or a handler's. Files and every
// other number are not.
int isatty(int fd) {
    if (fd < 0 || fd > 2) {
        errno = EBADF;
        return 0;
    }
    __stdio_std_init();
    if (std_kind[fd] == STD_FILE) {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}
