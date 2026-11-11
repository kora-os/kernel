// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helpers for the public libc headers; not a standard header. Names
// stay in the implementation's reserved space (_LIBC_*), and spellings work in
// C99 as well as C11 (MicroPython and the eventual on-OS compiler build C99).
#ifndef _LIBC_CDEFS_H
#define _LIBC_CDEFS_H

// The function never returns. Spelled as a GNU attribute, not _Noreturn, so
// it works in C99 and is unaffected by <stdnoreturn.h>'s `noreturn` macro.
#define _LIBC_NORETURN __attribute__((__noreturn__))

#endif // _LIBC_CDEFS_H
