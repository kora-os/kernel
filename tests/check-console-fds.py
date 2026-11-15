#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check that user programs do not name the console by file descriptor.

Programs reach the console through the libc's stdin, stdout and stderr, never
through read(0, ...) or write(1, ...): the console plan
(project_doc/console-plan.md) turns a program's stdin and stdout into handles
to a console instance, and only the streams will follow. See the "Userland
migration" rules in project_doc/libc-plan.md.

Scanned: user/*.c, user/libk and tests/user. A literal 0, 1 or 2 as the first
argument of read() or write() is an error, except for:

  ALLOWED           the lasting exceptions, each with its reason
  NOT_YET_MIGRATED  programs the migration has not reached; an entry whose
                    file no longer needs it must be removed (the check fails
                    until it is), so the list can only shrink
"""

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
SCANNED = ("user/*.c", "user/libk/*.[chS]", "tests/user/*.c")
CALL = re.compile(r"\b(read|write)\s*\(\s*([012])\s*,")

# file -> calls it may make, and why.
ALLOWED = {
    "user/libk/koraos.h": (
        {"read(0)", "write(1)"},
        "kgetline edits a line on the raw console, until the screen manager does"),
    "tests/user/threadprobe.c": (
        {"read(0)"},
        "a thread blocked in a console read is what the probe tests"),
}

# file -> the libc-plan.md step that migrates it.
NOT_YET_MIGRATED = {
    "tests/user/forbidprobe.c": 13,
    "tests/user/schedprobe.c": 13,
    "tests/user/sumprobe.c": 13,
    "tests/user/threadprobe.c": 13,
}


def strip_comments(text):
    """Blank out comments, keeping the line numbers."""
    def blank(match):
        return re.sub(r"[^\n]", " ", match.group(0))
    text = re.sub(r"/\*.*?\*/", blank, text, flags=re.S)
    return re.sub(r"//[^\n]*", blank, text)


def console_calls(path):
    """[(line, "write(1)"), ...] for every literal console fd in the file."""
    text = strip_comments(path.read_text(encoding="utf-8"))
    return [(text.count("\n", 0, m.start()) + 1, f"{m.group(1)}({m.group(2)})")
            for m in CALL.finditer(text)]


def main():
    errors = []
    files = sorted({p for pattern in SCANNED for p in ROOT.glob(pattern)})
    seen = set()
    for path in files:
        name = path.relative_to(ROOT).as_posix()
        seen.add(name)
        allowed = ALLOWED.get(name, (set(), ""))[0]
        extra = [(line, call) for line, call in console_calls(path) if call not in allowed]
        if name in NOT_YET_MIGRATED:
            if not extra:
                errors.append(f"{name}: no literal console fd left; remove it from "
                              f"NOT_YET_MIGRATED in tests/check-console-fds.py")
            continue
        for line, call in extra:
            stream = {"0": "stdin", "1": "stdout", "2": "stderr"}[call[-2]]
            errors.append(f"{name}:{line}: {call[:-1]}, ...) names the console by fd; use {stream}")
    for name in sorted((set(ALLOWED) | set(NOT_YET_MIGRATED)) - seen):
        errors.append(f"{name}: listed in tests/check-console-fds.py but not found")
    if errors:
        print("console fds: FAILED", *errors, sep="\n", file=sys.stderr)
        return 1
    print(f"console fds: {len(files)} files, {len(NOT_YET_MIGRATED)} not migrated yet, "
          f"{len(ALLOWED)} lasting exceptions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
