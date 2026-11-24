// SPDX-License-Identifier: GPL-3.0-or-later
//
// alloca for MicroPython's core (user/micropython/mpconfigport.h): not ISO C,
// so not in the libc. The compiler's builtin allocates in the caller's frame.
#pragma once

#define alloca(size) __builtin_alloca(size)
