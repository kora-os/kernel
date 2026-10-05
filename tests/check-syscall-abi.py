#!/usr/bin/env python3
"""Check the independent kernel definitions, libk definitions and SVC stubs."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent


def constants(path, prefix):
    text = (ROOT / path).read_text()
    pattern = rf"^#define\s+({prefix}\w+)\s+(0x[0-9a-fA-F]+|[0-9]+)[uU]?\b"
    return {name.upper(): int(value, 0) for name, value in re.findall(pattern, text, re.M)}


def main():
    errors = []
    kernel = constants("include/sys/syscall.h", "SYS_")
    user = constants("user/libk/abi.h", "SYS_")
    if kernel != user:
        for name in sorted(kernel.keys() | user.keys()):
            if kernel.get(name) != user.get(name):
                errors.append(f"{name}: kernel={kernel.get(name)}, libk={user.get(name)}")
    if len(set(kernel.values())) != len(kernel):
        errors.append("duplicate syscall numbers")
    for name, number in {"SYNC": 37, "UNLINK": 38, "MKDIR": 39, "RMDIR": 40, "RENAME": 41}.items():
        if kernel.get("SYS_" + name) != number:
            errors.append(f"filesystem syscall {name} must retain slot {number}")
    stubs = (ROOT / "user/libk/syscall.S").read_text()
    seen = set()
    for name, macro in re.findall(r"^SYSCALL\s+(\w+),\s*(SYS_\w+)", stubs, re.M):
        if macro != "SYS_" + name.upper() or macro not in user:
            errors.append(f"{name}: wrong or missing syscall macro {macro}")
        seen.add(macro)
    if re.search(r"^exit:\s*\n\s*mov x8, #SYS_EXIT", stubs, re.M):
        seen.add("SYS_EXIT")
    if seen != set(user):
        errors.append(f"SVC stubs differ from ABI: missing={sorted(set(user) - seen)}, extra={sorted(seen - set(user))}")
    fat = constants("include/fs/fat32.h", "FAT32_O_")
    libk = constants("user/libk/koraos.h", "O_")
    flags = {"RDONLY": 0, "WRONLY": 1, "RDWR": 2, "CREAT": 0x100,
             "TRUNC": 0x200, "APPEND": 0x400, "EXCL": 0x800}
    for name, value in flags.items():
        if fat.get("FAT32_O_" + name) != value or libk.get("O_" + name) != value:
            errors.append(f"open flag {name} must match {value:#x} in FAT and libk")
    if errors:
        print("syscall ABI: FAILED", *errors, sep="\n", file=sys.stderr)
        return 1
    print(f"syscall ABI: {len(kernel)} syscall numbers/stubs and {len(flags)} open flags agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
