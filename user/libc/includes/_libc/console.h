// SPDX-License-Identifier: GPL-3.0-or-later
//
// KoraOS standard handles and the console handler protocol; not a standard
// header. The libc uses it for its standard streams, libk's koraos.h exposes
// it to programs (console handlers among them), and the kernel mirrors
// struct std_handle (proc/task.h).
#ifndef _LIBC_CONSOLE_H
#define _LIBC_CONSOLE_H

// A program's standard input, output or error: the kernel console, one of
// its files, or a message port that a console handler serves.
#define STD_CONSOLE 0
#define STD_FILE 1  // value: an open fd of the program
#define STD_PORT 2  // value: a port id

struct std_handle {
    int kind;
    int value;
};

// What the libc sends to a console handler's port for a stream whose handle
// is STD_PORT: one packet per read or write, replied when it is done. The
// handler fills in `result`: bytes written or read (a read waits for at least
// one byte, as the console does), 0 at the end of input, or -1. It may use
// `buf` until it replies, and not after.
#define CON_WRITE 1
#define CON_READ 2

struct con_packet {
    int op;               // CON_WRITE or CON_READ
    long result;
    void *buf;
    unsigned long length;
};

#endif // _LIBC_CONSOLE_H
