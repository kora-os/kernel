#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
FLAGS=(-std=c11 -g -O1 -Wall -Wextra -Werror -fsanitize=address,undefined
       -fno-sanitize-recover=all -fno-omit-frame-pointer -DKORAOS_VIRT
       -I "$ROOT/include" -I "$ROOT/tests/host")
"${CC:-clang}" "${FLAGS[@]}" -DVIRTIO_HOST_TEST "$ROOT/tests/host/virtio_test.c" \
    "$ROOT/src/drivers/virtio.c" -o "$OUT/virtio_test"
"$OUT/virtio_test"
"${CC:-clang}" "${FLAGS[@]}" "$ROOT/tests/host/virtio_blk_test.c" \
    "$ROOT/src/drivers/virtio_blk.c" -o "$OUT/virtio_blk_test"
"$OUT/virtio_blk_test"
for scenario in n i c z r f t l h; do "$OUT/virtio_blk_test" "$scenario"; done
