// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython's configuration for KoraOS (project_doc/koracli-plan.md, step
// 0a): the core language, a REPL with MicroPython's own line editor, double
// floats and math (the libc has musl's libm). The port's essentials (GC heap
// size, stack limit, KeyboardInterrupt, os, time, files) are step 0b.
#pragma once

#include <stdint.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_EMACS_KEYS (1)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (0)  // no filesystem for Python yet (0b)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
// The exact repr needs long double, binary128 on AArch64, which is software
// arithmetic the toolchain has no library for (no compiler-rt).
#define MICROPY_FLOAT_FORMAT_IMPL (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_SYS_PLATFORM "koraos"
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ALLOC_PATH_MAX (256)
// The stack the program is linked with, checked (main.c sets the limit).
#define MICROPY_STACK_CHECK (1)
// The console in raw mode while the line editor runs (mphalport.h).
#define MICROPY_HAL_HAS_STDIO_MODE_SWITCH (1)

// No native code emitters: bytecode only.
#define MICROPY_EMIT_ARM (0)
#define MICROPY_EMIT_THUMB (0)
#define MICROPY_EMIT_X64 (0)
#define MICROPY_EMIT_X86 (0)

// The GC finds roots on the main thread's stack and in the callee-saved
// registers, which setjmp (the libc's, AArch64 assembly) stores where it can
// scan them. gchelper_generic.c's AArch64 path reads them through local
// register variables instead, which clang does not guarantee to read.
#define MICROPY_GCREGS_SETJMP (1)

#define MICROPY_HW_BOARD_NAME "KoraOS"
#define MICROPY_HW_MCU_NAME "AArch64"

// 64-bit machine: a pointer, a long and mp_int_t are all 64 bits.
typedef long mp_int_t;
typedef unsigned long mp_uint_t;
typedef long mp_off_t;

#define MP_SSIZE_MAX (0x7fffffffffffffffL)
#define MP_STATE_PORT MP_STATE_VM

// <alloca.h> is not ISO C, so the libc does not have it: the port's shim
// (user/micropython/shims) maps alloca to the compiler's builtin.
#include <alloca.h>
