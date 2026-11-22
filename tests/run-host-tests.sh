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
# sources it exercises. Extra compiler flags for one test go in TEST_FLAGS.
run_test() {
    local name="$1"
    shift
    local sources=()
    for src in "$@"; do
        sources+=("$ROOT/$src")
    done
    echo "== $name"
    if ! "$CC" "${CFLAGS[@]}" ${TEST_FLAGS[@]+"${TEST_FLAGS[@]}"} -o "$OUT/${name}_test" \
            "$ROOT/tests/host/${name}_test.c" "${sources[@]}"; then
        echo "   BUILD FAILED"
        failed=1
        return
    fi
    if ! "$OUT/${name}_test"; then
        failed=1
    fi
}

python3 "$ROOT/tests/check-syscall-abi.py" || failed=1
python3 "$ROOT/tests/check-console-fds.py" || failed=1
python3 "$ROOT/tests/run-write-image-tests.py" || failed=1
python3 "$ROOT/tests/run-ramdisk-image-tests.py" || failed=1
run_test blkdev src/fs/blkdev.c
# The console library (user/libconsole), built as the kernel builds it.
TEST_FLAGS=(-DKORAOS_KERNEL -I "$ROOT/user")
run_test term user/libconsole/term.c
run_test term_render user/libconsole/term_render.c user/libconsole/term.c
run_test edit user/libconsole/edit.c user/libconsole/keys.c user/libconsole/term.c
TEST_FLAGS=()
run_test printf src/lib/printf.c
run_test virt src/platform/virt.c
run_test placement src/proc/placement.c
run_test sd src/drivers/sd.c
run_test readahead src/drivers/readahead.c
run_test ramdisk_image src/fs/ramdisk_image.c
run_test input src/input/input.c
run_test keylog src/input/keylog.c src/lib/printf.c
run_test input_tty src/input/input_tty.c
run_test hid_keyboard src/input/hid_keyboard.c
TEST_FLAGS=(-DKORAOS_HOST_TEST)
run_test kmalloc src/mm/kmalloc.c src/mm/kmalloc_stress.c src/arch/spinlock.c
TEST_FLAGS=(-DKORAOS_HOST_TEST -pthread)
run_test spinlock src/arch/spinlock.c
# The same test on the compiler-atomic fallback (what non-arm64 hosts build).
TEST_FLAGS=(-DKORAOS_HOST_TEST -DSPINLOCK_PORTABLE -pthread -I "$ROOT/tests/host")
"$CC" "${CFLAGS[@]}" "${TEST_FLAGS[@]}" -o "$OUT/spinlock_portable_test" \
    "$ROOT/tests/host/spinlock_test.c" "$ROOT/src/arch/spinlock.c" && "$OUT/spinlock_portable_test" || failed=1
TEST_FLAGS=()
run_test user_mem src/proc/user_mem.c
run_test port src/ipc/port.c
"$ROOT/tests/run-user-tests.sh" || failed=1
"$ROOT/tests/run-filesystem-tests.sh" || failed=1
"$ROOT/tests/run-fat-write-tests.sh" || failed=1
"$ROOT/tests/run-fat-namespace-tests.sh" || failed=1
"$ROOT/tests/run-ramfb-tests.sh" || failed=1
"$ROOT/tests/run-virtio-tests.sh" || failed=1
"$ROOT/tests/run-virtio-input-tests.sh" || failed=1

if [ "$failed" -ne 0 ]; then
    echo "host tests: FAILED"
    exit 1
fi
echo "host tests: all passed"
