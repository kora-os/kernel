// SPDX-License-Identifier: GPL-3.0-or-later
//
// <unistd.h> for MicroPython's core, which includes it for ssize_t only (seven
// files of py/, unconditionally). Not ISO C, so not in the libc, and nothing
// else of POSIX is provided. The type is libk's (user/libk/koraos.h); C11
// allows the same typedef twice.
#pragma once

typedef long ssize_t;
