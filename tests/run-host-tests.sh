#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build and run the host-side unit tests: pure-logic kernel sources compiled
# with the host compiler under AddressSanitizer + UndefinedBehaviorSanitizer.
# No cross toolchain, QEMU or hardware needed. Exits non-zero on any failure.
#
# Usage: tests/run-host-tests.sh        (CC=... to pick the host compiler)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
CC="${CC:-clang}"

# -Wno-incompatible-library-redeclaration: the kernel's lib/string.h declares
# strlen() returning int, which differs from the host libc's size_t.
CFLAGS=(
    -std=c11 -g -O1
    -Wall -Wextra -Werror -Wno-incompatible-library-redeclaration
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
    -I "$ROOT/include" -I "$ROOT/tests/host"
    -DPRINTF_LONG_SUPPORT  # as in the kernel build (%l formats)
)

mkdir -p "$OUT"
failed=0

# run_test <name> <kernel sources...>: tests/host/<name>_test.c plus the kernel
# sources it exercises.
run_test() {
    local name="$1"
    shift
    local sources=()
    for src in "$@"; do
        sources+=("$ROOT/$src")
    done
    echo "== $name"
    if ! "$CC" "${CFLAGS[@]}" -o "$OUT/${name}_test" \
            "$ROOT/tests/host/${name}_test.c" "${sources[@]}"; then
        echo "   BUILD FAILED"
        failed=1
        return
    fi
    if ! "$OUT/${name}_test"; then
        failed=1
    fi
}

run_test term src/video/term.c
run_test printf src/lib/printf.c
run_test virt src/platform/virt.c
"$ROOT/tests/run-ramfb-tests.sh" || failed=1
"$ROOT/tests/run-virtio-tests.sh" || failed=1
"$ROOT/tests/run-virtio-input-tests.sh" || failed=1

if [ "$failed" -ne 0 ]; then
    echo "host tests: FAILED"
    exit 1
fi
echo "host tests: all passed"
