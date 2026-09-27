// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// The user-facing terminal: what user programs read on fd 0 and write on fd 1/2.
// Output goes to the framebuffer screen; input comes from the USB keyboard, with
// the UART as a fallback (see tty_serial_init). Kernel diagnostics (printf,
// console_log) stay on the UART and never appear here.
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
void tty_flush(void);

// Block until an input byte arrives from the keyboard or the UART (when routed
// here). Must be called with IRQs masked (from a syscall); IRQs are opened only
// while waiting.
char tty_getc(void);

// Queue a byte of keyboard input. Called from IRQ context by the USB keyboard
// driver; bytes are dropped if the queue is full.
void tty_input_push(char c);

#ifdef __cplusplus
}
#endif
