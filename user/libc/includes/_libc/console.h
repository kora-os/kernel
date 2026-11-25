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
// echoed by the console, the default), CON_RAW (keys as typed, no echo) or
// CON_EVENTS (each key press as a struct con_key_event, below). `buf` is
// unused; `result` is 0, or -1 for a handler without the mode.
#define CON_MODE 3
#define CON_COOKED 0
#define CON_RAW 1
#define CON_EVENTS 2
// The console's font: a PSF2 font in `buf` (`length` bytes, which the
// handler copies before it replies), or `buf` NULL for its built-in font.
// The console resizes to the cells the font gives. `result` is 0, or -1: not
// a font it can use, too little memory, or a handler without fonts.
#define CON_FONT 4
// A bar outside the text grid, which loses a row for it: `buf` is a struct
// con_bar (the handler copies its text before it replies). `result` is 0, or
// -1 for a handler without bars.
#define CON_BAR 5
// ESC as a break: with `length` 1, the ESC key no longer reaches the
// console's programs; the handler sends the break signal (SIG_BREAK in
// libk) to the program that asked instead, until it asks for 0 or ends.
// `buf` is unused; `result` is 0, or -1 for a handler without the setting.
#define CON_ESC_BREAK 6

#define CON_BAR_TOP 0
#define CON_BAR_BOTTOM 1

struct con_bar {
    int which;           // CON_BAR_TOP or CON_BAR_BOTTOM
    const char *text;    // UTF-8, `length` bytes; NULL takes the bar away
    unsigned long length;
};

// In CON_EVENTS mode a read gets whole events, oldest first, as many as fit
// (a read with room for none is refused with -1): each key pressed, with its
// position (a USB HID usage, KEY_* in libk), the modifiers held (INPUT_MOD_*)
// and the character it types under the layout (0 for none). Shift+PageUp and
// Shift+PageDown still scroll the console, and Ctrl+C is still its break.
struct con_key_event {
    unsigned int codepoint;
    unsigned short keycode;
    unsigned short modifiers;
};

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

// Set or take away a bar (CON_BAR) of the console handler that stdout is: 0,
// or -1 as for __con_font.
int __con_bar(int which, const char *text, unsigned long length);

// Turn ESC as a break (CON_ESC_BREAK) on or off for the console handler that
// stdin is: 0, or -1 as for __con_mode.
int __con_esc_break(int on);

// Read up to `max` key events from the console handler that stdin is, in
// CON_EVENTS mode, waiting for one: how many, 0 at the end of input, or -1
// (stdin is not a handler, or not in that mode). Not through stdin's buffer:
// a program reading events does not read stdin otherwise.
long __con_events(struct con_key_event *events, unsigned long max);

#endif // _LIBC_CONSOLE_H
