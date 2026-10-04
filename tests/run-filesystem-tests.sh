#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
python3 "$ROOT/tests/create-filesystem-fixtures.py" "$OUT/fs-fixtures"
FLAGS=(-fno-builtin -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-incompatible-library-redeclaration
       -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
       -I "$ROOT/include" -I "$ROOT/tests/host")
"${CC:-clang}" "${FLAGS[@]}" "$ROOT/tests/host/filesystem_test.c" \
    "$ROOT/src/fs/fat32.c" "$ROOT/src/fs/namespace.c" "$ROOT/src/fs/blkdev.c" -o "$OUT/filesystem_test"
"$OUT/filesystem_test" "$OUT/fs-fixtures"
