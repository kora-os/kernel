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
// The console's input mode, in `length`: CON_COOKED (lines, edited and
// echoed by the console, the default) or CON_RAW (keys as typed, no echo).
// `buf` is unused; `result` is 0, or -1 for a handler without modes.
#define CON_MODE 3
#define CON_COOKED 0
#define CON_RAW 1
// The console's font: a PSF2 font in `buf` (`length` bytes, which the
// handler copies before it replies), or `buf` NULL for its built-in font.
// The console resizes to the cells the font gives. `result` is 0, or -1: not
// a font it can use, too little memory, or a handler without fonts.
#define CON_FONT 4

struct con_packet {
    int op;               // CON_WRITE, CON_READ, CON_MODE or CON_FONT
    long result;
    void *buf;
    unsigned long length;
};

// Set the input mode (CON_COOKED or CON_RAW) of the console handler that
// stdin is: 0, or -1 when stdin is the kernel console (always raw), a file,
// or a handler that has no modes.
int __con_mode(int mode);

// Set the font (CON_FONT) of the console handler that stdout is: 0, or -1
// when stdout is the kernel console, a file, or a handler that refuses it.
int __con_font(const void *font, unsigned long size);

#endif // _LIBC_CONSOLE_H
