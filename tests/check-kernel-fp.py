#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that a kernel image never touches FP/SIMD registers outside the
routines that context switch them.

The kernel (Circle included) is built -mgeneral-regs-only: FP/SIMD registers
belong to EL0 tasks and are switched lazily, and kernel code, including
interrupt handlers, must leave them alone. A regression (a vendored file
without the flag, an assembly routine, a compiler change) would silently
corrupt user registers, most likely only on real hardware, so this script
disassembles the code that goes into each kernel.elf and fails on any
instruction that names a SIMD/FP register (v, q, d, s, h, b) or the
FPCR/FPSR system registers outside the allow-listed functions.

It reads the object files next to each kernel.elf (every object built for that
kernel, Circle included) rather than the linked image: the linker script folds
.rodata into .text, where tables would otherwise be decoded as instructions,
while in object files data sits in its own sections and literal pools in code
carry mapping symbols.

Usage: tests/check-kernel-fp.py build/debug/*/kernel.elf
Needs llvm-objdump (or set OBJDUMP).
"""

import glob
import os
import re
import shutil
import subprocess
import sys

# Functions allowed to use FP/SIMD: the lazy save/restore in src/arch/fpsimd.S.
ALLOWED = {"fpsimd_save", "fpsimd_load", "fpsimd_zero"}

FUNC = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
# Address, optional raw bytes (printed for data even with --no-show-raw-insn),
# then the mnemonic and operands.
INSN = re.compile(r"^\s*[0-9a-f]+:\s+(?:[0-9a-f]{2} )*\s*(\S+)\s*(.*)$")
FP_REG = re.compile(r"\b(?:[vqdshb](?:[0-9]|[12][0-9]|3[01]))(?:\.\w+)?\b|\bfp[cs]r\b", re.I)


def objdump():
    tool = os.environ.get("OBJDUMP")
    if tool:
        return tool
    for name in ("llvm-objdump", "llvm-objdump-18", "llvm-objdump-17"):
        if shutil.which(name):
            return name
    for prefix in ("/opt/homebrew/opt/llvm/bin", "/usr/local/opt/llvm/bin"):
        path = os.path.join(prefix, "llvm-objdump")
        if os.path.exists(path):
            return path
    sys.exit("check-kernel-fp: llvm-objdump not found (set OBJDUMP)")


def check(tool, elf):
    objects = sorted(glob.glob(os.path.join(os.path.dirname(elf), "CMakeFiles", "**", "*.obj"),
                               recursive=True))
    if not objects:
        sys.exit("check-kernel-fp: no object files next to %s" % elf)
    out = subprocess.run([tool, "-d", "--no-show-raw-insn", *objects], check=True,
                         capture_output=True, text=True).stdout
    func = "?"
    hits = []
    for line in out.splitlines():
        m = FUNC.match(line)
        if m:
            func = m.group(1)
            continue
        m = INSN.match(line)
        if not m or func in ALLOWED:
            continue
        mnemonic, operands = m.group(1), m.group(2).split("//")[0]
        if mnemonic.startswith("."):
            continue  # data (literal pools, tables) printed as directives
        if FP_REG.search(operands):
            hits.append("%s: %s %s" % (func, mnemonic, operands.strip()))
    return hits


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    tool = objdump()
    failed = False
    for elf in sys.argv[1:]:
        hits = check(tool, elf)
        if hits:
            failed = True
            print("%s: %d FP/SIMD instruction(s) outside %s:" % (elf, len(hits), sorted(ALLOWED)))
            for hit in hits[:40]:
                print("  " + hit)
        else:
            print("%s: integer-only OK" % elf)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
