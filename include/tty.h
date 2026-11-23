// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// The user-facing terminal: what user programs read on fd 0 and write on fd 1/2.
// Output goes to the framebuffer screen; input comes from the keyboard. Kernel
// diagnostics (printf, console_log) stay on the UART and never appear here.
// Once a screen manager runs (user/screens.c), programs have consoles of their
// own on its screens, and this terminal is the boot and panic fallback.
//
// Keyboards do not call in here: they post key events to the input event queue
// (input/input.h), and the tasks reading the terminal turn those into bytes
// (input/input_tty.h). That adapter is temporary; see
// project_doc/console-plan.md.
//
// When no screen is up, output goes to the UART instead; with the debug
// console's conlog on (the default under QEMU), it goes to both.

#ifdef __cplusplus
extern "C" {
#endif

// Serial input is the kernel debug console's (console.c), never this
// terminal's. tty_serial_init() hooks the UART receive interrupt;
// tty_poll_serial() drains pending bytes into the debug console and is safe to
// call from any context.
void tty_serial_init(void);
void tty_poll_serial(void);

// Write one character to the terminal. Screen output that scrolls is batched:
// call tty_flush() when a burst of output is done.
void tty_putc(char c);
// Write `len` bytes the way tty_putc does each, with the serial copy (QEMU
// builds, or no screen) going out in one piece under the serial output lock,
// so other cores' lines never land in the middle of it.
void tty_write(const char *buf, size_t len);

// Write `len` bytes to the serial line, '\n' as CR LF, in one piece under the
// serial output lock: the screen terminal's copy (conlog), and the consoles of
// user programs (the conlog syscall).
void tty_serial_copy(const char *buf, size_t len);
void tty_flush(void);

// Block until an input byte arrives from the keyboard. Must be called with IRQs masked (from a syscall); IRQs are opened only
// while waiting. Shift+PageUp and Shift+PageDown scroll the screen's history
// meanwhile. Returns the byte (0..255), or -1 if the waiting task was killed.
int tty_getc(void);

// Raw console read, behind read(0, ...): block until at least one byte arrives,
// then take whatever else is already queued, up to `len` (> 0). No echo, no
// line buffering and no translation: line editing is a console's business
// (user/libconsole/edit.c). Same calling rules as tty_getc. Returns the byte count,
// or -1 if the waiting task was killed.
long tty_read(char *buf, size_t len);

// Queue a byte of input that is not a key: the status replies of the
// terminal (user/libconsole/term.c). Callable from IRQ context;
// bytes are dropped if the queue is full.
void tty_input_push(char c);

#ifdef __cplusplus
}
#endif
