# SPDX-License-Identifier: GPL-3.0-or-later
#
# The unix port of MicroPython as a host stand-in for KoraOS's port
# (tests/run-micropython-tests.sh): the minimal variant's build, with none of
# the unix port's extras (as third_party/micropython/ports/unix/variants/minimal).

FROZEN_MANIFEST =

MICROPY_PY_BTREE = 0
MICROPY_PY_FFI = 0
MICROPY_PY_SOCKET = 0
MICROPY_PY_THREAD = 0
MICROPY_PY_TERMIOS = 0
MICROPY_PY_SSL = 0
MICROPY_USE_READLINE = 0

MICROPY_VFS_FAT = 0
MICROPY_VFS_LFS1 = 0
MICROPY_VFS_LFS2 = 0
