#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -I "$ROOT/include" -I "$ROOT/tests/host" \
    "$ROOT/tests/host/virtio_keymap_test.c" "$ROOT/src/drivers/virtio_keymap.c" \
    -o "$OUT/virtio_keymap_test"
"$OUT/virtio_keymap_test"
"${CC:-clang}" -std=c11 -g -O1 -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
    -DKORAOS_VIRT -DKORAOS_VIRTIO_INPUT -DVIRTIO_INPUT_HOST_TEST \
    -I "$ROOT/include" -I "$ROOT/tests/host" \
    "$ROOT/tests/host/virtio_input_test.c" "$ROOT/src/drivers/virtio_input.c" \
    "$ROOT/src/drivers/virtio_keymap.c" -o "$OUT/virtio_input_test"
for scenario in normal pointer bad-config queue-failure; do
    "$OUT/virtio_input_test" "$scenario"
done
