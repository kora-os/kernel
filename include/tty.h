// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// The user-facing terminal: what user programs read on fd 0 and write on fd 1/2.
// Output goes to the framebuffer screen; input comes from the keyboard, with
// the UART as a fallback (see tty_serial_init). Kernel diagnostics (printf,
// console_log) stay on the UART and never appear here.
//
// Keyboards do not call in here: they post key events to the input event queue
// (input/input.h), and the tasks reading the terminal turn those into bytes
// (input/input_tty.h). That adapter is temporary; see
// project_doc/console-plan.md.
//
// Under QEMU (no USB keyboard, usually no visible screen), or when no screen is
// up, output is mirrored to the UART so the shell stays usable over serial.

#ifdef __cplusplus
extern "C" {
#endif

// Serial input: the UART is shared by the kernel debug console (console.c) and,
// as a fallback keyboard, this terminal; Ctrl-T switches between them. By
// default it is the debug console on hardware and the terminal under QEMU.
// tty_serial_init() hooks the UART receive interrupt; tty_poll_serial() drains
// and routes pending bytes and is safe to call from any context.
void tty_serial_init(void);
void tty_poll_serial(void);

// Write one character to the terminal. Screen output that scrolls is batched:
// call tty_flush() when a burst of output is done.
void tty_putc(char c);
// Write `len` bytes the way tty_putc does each, with the serial copy (QEMU
// builds, or no screen) going out in one piece under the serial output lock,
// so other cores' lines never land in the middle of it.
void tty_write(const char *buf, size_t len);
void tty_flush(void);

// Block until an input byte arrives from the keyboard or the UART (when routed
// here). Must be called with IRQs masked (from a syscall); IRQs are opened only
// while waiting. Shift+PageUp and Shift+PageDown scroll the screen's history
// meanwhile. Returns the byte (0..255), or -1 if the waiting task was killed.
int tty_getc(void);

// Raw console read, behind read(0, ...): block until at least one byte arrives,
// then take whatever else is already queued, up to `len` (> 0). No echo, no
// line buffering and no translation: line editing is the reader's business
// (libk's kgetline). Same calling rules as tty_getc. Returns the byte count,
// or -1 if the waiting task was killed.
long tty_read(char *buf, size_t len);

// Queue a byte of input that is not a key: serial input routed here, and the
// status replies of the terminal (video/term.c). Callable from IRQ context;
// bytes are dropped if the queue is full.
void tty_input_push(char c);

#ifdef __cplusplus
}
#endif
