#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
python3 "$ROOT/tests/create-fat-write-fixtures.py" create "$OUT/write-fixtures"
FLAGS=(-fno-builtin -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-incompatible-library-redeclaration
       -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
       -I "$ROOT/include" -I "$ROOT/tests/host")
"${CC:-clang}" "${FLAGS[@]}" "$ROOT/tests/host/fat_write_test.c" \
    "$ROOT/src/fs/fat32.c" "$ROOT/src/fs/blkdev.c" -o "$OUT/fat_write_test"
"$OUT/fat_write_test" "$OUT/write-fixtures"
python3 "$ROOT/tests/create-fat-write-fixtures.py" verify "$OUT/write-fixtures"
