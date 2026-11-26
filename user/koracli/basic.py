# SPDX-License-Identifier: GPL-3.0-or-later
#
# What KoraCLI's BASIC dialects share (project_doc/koracli-spec.md §6): the
# program store, BASIC's errors, the output with its column (for PRINT's
# zones and TAB), and the random numbers. The dialects are modules of their
# own: classic.py for now, BBC BASIC in phase 3.

MAX_LINE = 63999


class BasicError(Exception):
    """A BASIC error, by the dialect's name for it ("SYNTAX", "TYPE
    MISMATCH"), with the line it belongs to when that is not the line
    running (a READ reports the DATA line)."""

    def __init__(self, name, line=None):
        super().__init__(name)
        self.name = name
        self.line = line


class Program:
    """The program's lines, by number, stored as typed (unchecked: mistakes
    show when the program runs), with the dialect's keywords in capitals."""

    def __init__(self, normalize=None):
        self.lines = {}
        self.version = 0  # changes with every edit: CONT and the line order
        self.normalize = normalize

    def enter(self, text):
        """A program line: "10 PRINT X" stores line 10, "10" alone deletes
        it. The number and the line as stored ("" for a deletion)."""
        i = 0
        while i < len(text) and text[i].isdigit():
            i += 1
        number = int(text[:i])
        if number > MAX_LINE:
            raise BasicError("SYNTAX")
        rest = text[i:].strip()
        if rest:
            if self.normalize is not None:
                rest = self.normalize(rest)
            self.lines[number] = rest
        elif number in self.lines:
            del self.lines[number]
        else:
            return number, rest
        self.version += 1
        return number, rest

    def clear(self):
        self.lines = {}
        self.version += 1

    def numbers(self):
        return sorted(self.lines)


class Output:
    """Where BASIC prints, and the column it has reached there (0 at a line's
    start), wrapped at the screen's width as the terminal wraps."""

    def __init__(self, write, width=80):
        self._write = write
        self.width = max(width, 8)
        self.col = 0

    def write(self, s):
        if not s:
            return
        self._write(s)
        nl = s.rfind("\n")
        col = len(s) - nl - 1 if nl >= 0 else self.col + len(s)
        self.col = col % self.width

    def newline_if_needed(self):
        if self.col:
            self.write("\n")


class Random:
    """xorshift32: RND's numbers, the same sequence for the same seed (MicroPython
    as KoraOS builds it has no random module)."""

    def __init__(self, seed=1):
        self.seed(seed)

    def seed(self, n):
        n = int(n) & 0xFFFFFFFF
        self.state = n or 0x2545F491

    def next(self):
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x
        return x / 4294967296.0
