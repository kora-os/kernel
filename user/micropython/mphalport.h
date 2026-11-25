// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython's HAL on KoraOS (user/micropython/main.c): the standard streams
// of the libc, and the console's modes.
#pragma once

// Ctrl+C reaches a program as a break (SIG_BREAK in libk), not as a byte, so
// there is no interrupt character to watch for: hal.c turns the break into
// KeyboardInterrupt.
static inline void mp_hal_set_interrupt_char(int c) {
    (void)c;
}

// MicroPython's line editor reads keys as typed: the console in raw mode
// while it runs, back in cooked mode when it ends (MICROPY_HAL_HAS_STDIO_MODE_SWITCH).
void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);

// The console's mode, set through the HAL so that what it reads is read in
// that mode (hal.c): 0, or -1 when stdin is not a console handler with it.
int kora_console_mode(int mode);
int kora_console_get_mode(void);

struct con_key_event;
// CON_EVENTS mode: the next key press, waiting for it: 1, 0 at the end of
// input, -1 for a break (Ctrl+C, or ESC as a break), -2 when stdin is not a
// console handler in that mode.
int kora_read_key(struct con_key_event *event);
