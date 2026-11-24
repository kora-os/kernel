// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython's HAL on KoraOS (user/micropython/main.c): the standard streams
// of the libc, and the console's modes.
#pragma once

// Ctrl+C reaches a program as a break (SIG_BREAK in libk), not as a byte, so
// there is no interrupt character to watch for. KeyboardInterrupt from a
// break is step 0b.
static inline void mp_hal_set_interrupt_char(int c) {
    (void)c;
}

// MicroPython's line editor reads keys as typed: the console in raw mode
// while it runs, back in cooked mode when it ends (MICROPY_HAL_HAS_STDIO_MODE_SWITCH).
void mp_hal_stdio_mode_raw(void);
void mp_hal_stdio_mode_orig(void);
