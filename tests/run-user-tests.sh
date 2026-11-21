#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build and run the host tests of the user programs and of libk's console
# code (user/tests). The programs are compiled with the host compiler, under
# AddressSanitizer + UndefinedBehaviorSanitizer, against the KoraOS libc
# itself: its headers shadow the host's and its sources are built for the
# host with their symbols renamed (user/libc/tests/host_rename.h). The kernel
# is the libc tests' fake one (user/libc/tests/fake_syscall.c), which libk's
# syscall stubs reach through user/tests/libk_host.c. So a program's printf,
# its file streams and its raw read() all end in the same fake console and
# in-memory filesystem. No cross toolchain or QEMU needed.
#
# Usage: tests/run-user-tests.sh        (CC=... to pick the host compiler)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests/user"
CC="${CC:-clang}"
LIBC="$ROOT/user/libc"

# As the libc's own host tests (LIBC_HOST_FLAGS in user/libc/CMakeLists.txt).
LIBC_FLAGS=(
    -std=c11 -g -O1 -Wall -Wextra -Werror
    -fno-builtin -fsanitize=address,undefined -fno-sanitize-recover=all
    -fno-omit-frame-pointer
    -I "$LIBC/includes" -I "$LIBC/tests" -I "$LIBC/src/internal"
    -include "$LIBC/tests/host_rename.h"
)
USER_FLAGS=(
    "${LIBC_FLAGS[@]}"
    -I "$ROOT/user" -I "$ROOT/user/tests"
    -include "$ROOT/user/tests/libk_host_rename.h"
    -DLIBK_HOST_TEST  # koraos.h: the test supplies uptime_us()
    -pthread          # host threads stand in for KoraOS threads (libk_threads)
)

mkdir -p "$OUT/libc"
failed=0

# The whole libc and the fake kernel, once, as host objects. All but src/math:
# no user program calls <math.h> yet, and that directory needs flags of its
# own and has AArch64-only files (see user/libc/CMakeLists.txt, whose
# math_test covers it).
objects=()
while IFS= read -r src; do
    name="${src#"$LIBC/"}"
    obj="$OUT/libc/${name//\//_}.o"
    "$CC" "${LIBC_FLAGS[@]}" -c -o "$obj" "$src"
    objects+=("$obj")
done < <(find "$LIBC/src" -name '*.c' -not -path '*/math/*' | sort; echo "$LIBC/tests/fake_syscall.c")
"$CC" "${USER_FLAGS[@]}" -c -o "$OUT/libk_host.o" "$ROOT/user/tests/libk_host.c"
objects+=("$OUT/libk_host.o")

# run_test <name> [libk sources...]: user/tests/<name>_test.c, which includes
# the program it tests, plus the libk sources that program links.
run_test() {
    local name="$1"
    shift
    local sources=()
    for src in "$@"; do
        sources+=("$ROOT/$src")
    done
    echo "== user/$name"
    if ! "$CC" "${USER_FLAGS[@]}" -o "$OUT/${name}_test" "$ROOT/user/tests/${name}_test.c" \
            ${sources[@]+"${sources[@]}"} "${objects[@]}"; then
        echo "   BUILD FAILED"
        failed=1
        return
    fi
    if ! "$OUT/${name}_test"; then
        failed=1
    fi
}

run_test libk_console
run_test libk_threads user/libk/mutex.c user/libk/job.c user/libk/heap.c
run_test tools
run_test demos user/libk/job.c user/libscreen/screen.c
run_test condemo user/libconsole/term.c user/libconsole/term_render.c user/libscreen/screen.c
run_test screens user/libscreen/screen.c
run_test cp
run_test shell_input

if [ "$failed" -ne 0 ]; then
    echo "user tests: FAILED"
    exit 1
fi
echo "user tests: all passed"
