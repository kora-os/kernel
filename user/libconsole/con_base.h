// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The console library (libconsole) builds in two places: into the kernel,
// for its own screen terminal, and into user programs. This header gives it
// the fixed-width types, bool and the mem* functions from whichever side it
// is built for. The kernel build (and the host tests, which run kernel code)
// defines KORAOS_KERNEL; user programs get the ISO headers of user/libc.

#ifdef KORAOS_KERNEL
#include "common.h"
#include "lib/string.h"
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif
