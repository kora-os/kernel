// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Report a fatal kernel error on the UART and stop this core: IRQs masked,
// then WFI forever. Output bypasses the printf lock, so a panic inside
// printf, or while another core holds the lock, still gets its message out.
void panic(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));

// Make this core's printf bypass the lock from now on (fault handlers that
// print several lines before halting).
void panic_begin(void);

// Serialize whole printf calls across cores and interrupt handlers (IRQ-safe
// lock, bypassed while panicking). Called once at boot.
void printf_lock_init(void);
