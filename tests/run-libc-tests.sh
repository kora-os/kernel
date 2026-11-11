#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build and test the libc (user/libc) as a standalone project:
#   1. cross-compile koraos_libc for AArch64 with the user program flags,
#   2. link the whole archive into a PIE, failing on any undefined symbol,
#   3. compile every public header alone for AArch64 (C99, C11, C17),
#   4. build and run the host unit tests under ASan/UBSan.
# Exits non-zero on any failure.
#
# Usage: tests/run-libc-tests.sh        (BUILD_DIR=... to pick the build directory)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/libc}"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

cmake -S "$ROOT/user/libc" -B "$BUILD_DIR" -G "Unix Makefiles" >/dev/null
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target koraos_libc libc_link_check libc_header_check
cmake --build "$BUILD_DIR" --target libc_tests
