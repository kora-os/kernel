#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the unix port of the vendored MicroPython (third_party/micropython)
# with KoraOS's variant (user/micropython/unix/koraos: the language of KoraOS's
# port) and run the Python tests in tests/micropython on it:
# each NAME.py must print exactly NAME.py.exp. Python code for KoraOS is
# tested on MicroPython itself, from the same pinned tree, not on CPython, so
# MicroPython's differences show (project_doc/koracli-plan.md).
#
# Usage: tests/run-micropython-tests.sh   (needs a host C compiler and make)

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests/micropython-unix"
TAG="$(sed -n 's/^set(MICROPY_TAG "\(.*\)")$/\1/p' "$ROOT/cmake/micropython.cmake")"
HASH="$(sed -n 's/^set(MICROPY_HASH "\(.*\)")$/\1/p' "$ROOT/cmake/micropython.cmake")"

# The variant has no threads, but on macOS the port's main.c still calls a
# thread function (mp_thread_set_realtime, upstream as of v1.29.0): there it
# is built with threads.
# Upstream code is left as it is: a newer compiler's warnings in it are not
# errors (the unix port builds with -Werror).
extra=(CFLAGS_EXTRA=-Wno-error)
if [ "$(uname -s)" = Darwin ]; then
    extra+=(MICROPY_PY_THREAD=1)
fi

echo "== building the MicroPython unix port ($TAG, koraos variant)"
MICROPY_GIT_TAG="$TAG" MICROPY_GIT_HASH="$HASH" \
    make -s -C "$ROOT/third_party/micropython/ports/unix" VARIANT_DIR="$ROOT/user/micropython/unix/koraos" \
        BUILD="$OUT" "${extra[@]}" -j"$(getconf _NPROCESSORS_ONLN)" \
        > "$OUT.log" 2>&1 || { tail -30 "$OUT.log"; echo "micropython tests: BUILD FAILED"; exit 1; }

failed=0
for script in "$ROOT"/tests/micropython/*.py; do
    name="$(basename "$script")"
    if diff -u "$script.exp" <("$OUT/micropython" "$script" 2>&1); then
        echo "ok   $name"
    else
        echo "FAIL $name"
        failed=1
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "micropython tests: FAILED"
    exit 1
fi
echo "micropython tests: all passed"
