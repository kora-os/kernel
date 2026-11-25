// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython's configuration for KoraOS (project_doc/koracli-plan.md, steps
// 0a and 0b): the core language, a REPL with MicroPython's own line editor,
// double floats and math (the libc has musl's libm), a GC heap that grows,
// KeyboardInterrupt from a break, files, `os`, `time` and the `kora` module.
#pragma once

#include <stdint.h>
#include <stdlib.h>

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_FINALISER (1)  // a file nobody closed is closed when collected
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_EMACS_KEYS (1)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
// The exact repr needs long double, binary128 on AArch64, which is software
// arithmetic the toolchain has no library for (no compiler-rt).
#define MICROPY_FLOAT_FORMAT_IMPL (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ENABLE_SOURCE_LINE (1)  // tracebacks give line numbers
#define MICROPY_PY_BUILTINS_COMPILE (1)  // KoraCLI runs blocks as the REPL does
#define MICROPY_PY_FUNCTION_ATTRS (1)  // fn.__name__, for @kora.cmd.register
#define MICROPY_ALLOC_PATH_MAX (256)

// sys: argv (a script's arguments), path (modules), exit.
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_PY_SYS_PLATFORM "koraos"

// A script's exit code: 0, 1 for an exception, 130 for KeyboardInterrupt,
// or sys.exit()'s value (main.c).
#define MICROPY_PYEXEC_ENABLE_EXIT_CODE_HANDLING (1)

// Modules from files: `import` reads them with the libc (file.c), from the
// directories of sys.path.
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)

// `os` is the port's (modos.c) over KoraOS's volumes and assigns, not
// extmod's, which works through a VFS; `time` is extmod's (modtime.c), on
// the HAL's ticks and delays.
#define MICROPY_PY_OS (0)
#define MICROPY_VFS (0)
#define MICROPY_PY_TIME (1)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (0)  // no wall clock yet
#define MICROPY_PY_TIME_TIME_TIME_NS (0)

// The GC heap grows: it starts at MICROPY_KORA_HEAP_FIRST bytes from malloc
// and adds an area (doubling the total each time) when an allocation does
// not fit; an area left empty is given back (hal.c says how much may come).
#define MICROPY_GC_SPLIT_HEAP (1)
#define MICROPY_GC_SPLIT_HEAP_AUTO (1)
#define MP_PLAT_ALLOC_HEAP(size) malloc(size)
#define MP_PLAT_FREE_HEAP(ptr) free(ptr)
#define MICROPY_KORA_HEAP_FIRST (256 * 1024)

// The stack the program is linked with, checked (main.c sets the limit from
// what the kernel gave it).
#define MICROPY_STACK_CHECK (1)

// Ctrl+C is a break (SIG_BREAK), which the program handles: the VM looks
// for one now and then (hal.c, at most every 10 ms) and raises
// KeyboardInterrupt; a read from the console ends with Ctrl+C.
#define MICROPY_KBD_EXCEPTION (1)
void kora_vm_hook(void);
#define MICROPY_VM_HOOK_COUNT (64)
#define MICROPY_VM_HOOK_INIT static unsigned int vm_hook_divisor = MICROPY_VM_HOOK_COUNT;
#define MICROPY_VM_HOOK_POLL \
    if (--vm_hook_divisor == 0) { \
        vm_hook_divisor = MICROPY_VM_HOOK_COUNT; \
        kora_vm_hook(); \
    }
#define MICROPY_VM_HOOK_LOOP MICROPY_VM_HOOK_POLL
#define MICROPY_VM_HOOK_RETURN MICROPY_VM_HOOK_POLL

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
