#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build/host-tests"
mkdir -p "$OUT"
python3 "$ROOT/tests/create-fat-namespace-fixtures.py" create "$OUT/namespace-fixtures"
FLAGS=(-fno-builtin -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-incompatible-library-redeclaration
       -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer
       -I "$ROOT/include" -I "$ROOT/tests/host")
"${CC:-clang}" "${FLAGS[@]}" "$ROOT/tests/host/fat_namespace_test.c" \
    "$ROOT/src/fs/fat32.c" "$ROOT/src/fs/namespace.c" "$ROOT/src/fs/blkdev.c" -o "$OUT/fat_namespace_test"
"$OUT/fat_namespace_test" "$OUT/namespace-fixtures"
python3 "$ROOT/tests/create-fat-namespace-fixtures.py" verify "$OUT/namespace-fixtures"
