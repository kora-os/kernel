#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -DKORAOS_VIRT -DKORAOS_RAMFB -I "$ROOT/include" -I "$ROOT/tests/host" \
    "$ROOT/tests/host/ramfb_test.c" "$ROOT/src/video/ramfb.c" \
    "$ROOT/src/video/framebuffer.c" -o "$OUT/ramfb_test"
"$OUT/ramfb_test"
