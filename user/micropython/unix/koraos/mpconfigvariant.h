// SPDX-License-Identifier: GPL-3.0-or-later
//
// The unix port of MicroPython with the language of KoraOS's port
// (user/micropython/mpconfigport.h): the core features, big integers, double
// floats with the approximate repr, Unicode strings. What a Python test runs
// on the host (tests/run-micropython-tests.sh) then behaves as on KoraOS.
// Keep the two in step.

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_CORE_FEATURES)

#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_DOUBLE)
#define MICROPY_FLOAT_FORMAT_IMPL (MICROPY_FLOAT_FORMAT_IMPL_APPROX)
#define MICROPY_PY_MATH (1)
#define MICROPY_PY_BUILTINS_STR_UNICODE (1)
#define MICROPY_PY_SYS_EXIT (1)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_ENABLE_SOURCE_LINE (1)
#define MICROPY_PY_BUILTINS_COMPILE (1)
#define MICROPY_PY_FUNCTION_ATTRS (1)
#define MICROPY_STACK_CHECK (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_REPL_EMACS_KEYS (1)

// What the unix port itself needs to run a script (as the minimal variant).
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_PY_OS (1)
#define MICROPY_KBD_EXCEPTION (1)
// No `vfs` module (as the minimal variant): the vendored subset carries the
// unix port's POSIX VFS, not FAT's or the others.
#define MICROPY_PY_VFS (0)

#define MICROPY_EMIT_X86 (0)
#define MICROPY_EMIT_X64 (0)
#define MICROPY_EMIT_THUMB (0)
#define MICROPY_EMIT_ARM (0)
