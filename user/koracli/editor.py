# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's line editor (project_doc/koracli-spec.md §11), line-oriented
# style: it edits a block of lines at the prompt from key events and says
# when it is to run. It knows the terminal only by its width and draws
# through `write` (xterm sequences), so it runs on the host too
# (tests/micropython).
#
#   Enter          a line that is complete runs; a block that a compound
#                  statement starts goes on with "... ", keeping one blank
#                  line, and runs at the second blank line in a row
#   Ctrl+Enter     runs the block as it is
#   Left, Right, Home, End (Ctrl+A, Ctrl+E), Backspace, Delete, Ctrl+K,
#   Ctrl+U         editing, across the lines of a block
#   Up, Down       the block's lines; past its first or last, the history
#   PgUp, PgDn     the history, a whole block at a time
#   Tab            spaces to the next multiple of four
#   Ctrl+D         on an empty block, the end; otherwise Delete
#
# feed() returns None, or what the shell is to do: ("run", text),
# ("eof",), ("key", name) for F1 and F2.

import keys

INDENT = 4
HISTORY_SIZE = 100

COMPOUND = ("if", "while", "for", "try", "with", "def", "class", "async")


def is_compound(line):
    """Whether a line starts a compound statement (as MicroPython's REPL
    decides): a block follows it."""
    s = line.lstrip()
    if s.startswith("@"):
        return True
    for word in COMPOUND:
        if s.startswith(word) and (len(s) == len(word) or not (s[len(word)].isalpha() or
                                   s[len(word)].isdigit() or s[len(word)] == "_")):
            return True
    return False


def open_brackets(text):
    """Whether `text` leaves a bracket or a triple-quoted string open, so
    more lines must come."""
    depth = 0
    quote = None
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if quote:
            if c == "\\":
                i += 2
                continue
            if text.startswith(quote, i):
                i += len(quote)
                quote = None
                continue
            if c == "\n" and len(quote) == 1:
                quote = None  # an unclosed short string ends at the line
        elif c == "#":
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif c in "'\"":
            quote = text[i:i + 3] if text[i:i + 3] in ("'''", '"""') else c
            i += len(quote)
            continue
        elif c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        i += 1
    return depth > 0 or (quote is not None and len(quote) == 3)


def code_part(line):
    """The line without a trailing comment (not looking into strings)."""
    i = line.find("#")
    return line if i < 0 else line[:i]


class History:
    """Blocks run, oldest first."""

    def __init__(self):
        self.blocks = []

    def add(self, text):
        if text.strip() and (not self.blocks or self.blocks[-1] != text):
            self.blocks.append(text)
            if len(self.blocks) > HISTORY_SIZE:
                self.blocks.pop(0)


class Editor:
    def __init__(self, write, width, history=None):
        self.write = write
        self.width = max(width, 8)
        self.history = history if history is not None else History()
        self.prompt = ">>> "
        self.more = "... "
        self.routed = None
        self.lines = [""]
        self.row = 0
        self.col = 0
        self.shown_row = 0  # the cursor's screen row, from the block's first
        self.browse = None
        self.draft = None

    # --- the block on the screen ---

    def start(self, prompt, more, routed=None):
        """A new block at a new prompt. `routed(line)` says whether a first
        line goes elsewhere whole (a prefix): it runs at Enter."""
        self.prompt = prompt
        self.more = more
        self.routed = routed
        self.lines = [""]
        self.row = self.col = 0
        self.shown_row = 0
        self.browse = None
        self.draft = None
        self.write(prompt)

    def set_prompts(self, prompt, more, routed=None):
        """New prompts for the block being typed (a mode change), redrawn."""
        self.prompt = prompt
        self.more = more
        self.routed = routed
        self.redraw()

    def text(self):
        return "\n".join(self.lines)

    def _prefix(self, i):
        return self.prompt if i == 0 else self.more

    def _rows(self, n):
        return 1 if n == 0 else (n - 1) // self.width + 1

    def _place(self, i, col):
        """Screen row (from the block's first) and column of position `col`
        of line `i`."""
        row = 0
        for j in range(i):
            row += self._rows(len(self._prefix(j)) + len(self.lines[j]))
        offset = len(self._prefix(i)) + col
        return row + offset // self.width, offset % self.width

    def redraw(self):
        out = []
        if self.shown_row > 0:
            out.append("\x1b[%dA" % self.shown_row)
        out.append("\r\x1b[J")
        out.append("\n".join(self._prefix(i) + line for i, line in enumerate(self.lines)))
        # The cursor is at the end of the last line now: where is that?
        last = len(self.lines) - 1
        end_row = 0
        for j in range(last):
            end_row += self._rows(len(self._prefix(j)) + len(self.lines[j]))
        end_len = len(self._prefix(last)) + len(self.lines[last])
        end_row += self._rows(end_len) - 1
        row, col = self._place(self.row, self.col)
        if row < end_row:
            out.append("\x1b[%dA" % (end_row - row))
        elif row > end_row:
            out.append("\x1b[%dB" % (row - end_row))
        out.append("\r")
        if col:
            out.append("\x1b[%dC" % col)
        self.shown_row = row
        self.write("".join(out))

    def _to_end(self):
        """The cursor after the last line, for what comes after the block."""
        self.row = len(self.lines) - 1
        self.col = len(self.lines[self.row])
        self.redraw()

    # --- editing ---

    def _insert(self, s):
        line = self.lines[self.row]
        self.lines[self.row] = line[:self.col] + s + line[self.col:]
        self.col += len(s)

    def _backspace(self):
        if self.col > 0:
            line = self.lines[self.row]
            self.lines[self.row] = line[:self.col - 1] + line[self.col:]
            self.col -= 1
        elif self.row > 0:
            self.col = len(self.lines[self.row - 1])
            self.lines[self.row - 1] += self.lines.pop(self.row)
            self.row -= 1

    def _delete(self):
        line = self.lines[self.row]
        if self.col < len(line):
            self.lines[self.row] = line[:self.col] + line[self.col + 1:]
        elif self.row < len(self.lines) - 1:
            self.lines[self.row] += self.lines.pop(self.row + 1)

    def _left(self):
        if self.col > 0:
            self.col -= 1
        elif self.row > 0:
            self.row -= 1
            self.col = len(self.lines[self.row])

    def _right(self):
        if self.col < len(self.lines[self.row]):
            self.col += 1
        elif self.row < len(self.lines) - 1:
            self.row += 1
            self.col = 0

    def _vertical(self, step):
        to = self.row + step
        if 0 <= to < len(self.lines):
            self.row = to
            self.col = min(self.col, len(self.lines[to]))
        else:
            self._history(step)

    def _history(self, step):
        blocks = self.history.blocks
        if self.browse is None:
            if step > 0 or not blocks:
                return
            self.draft = self.text()
            self.browse = len(blocks)
        to = self.browse + step
        if to < 0:
            return
        self.browse = to
        if to >= len(blocks):
            text = self.draft
            self.browse = None
        else:
            text = blocks[to]
        self.lines = text.split("\n")
        self.row = len(self.lines) - 1
        self.col = len(self.lines[self.row])

    def _indent_after(self, i):
        """The indentation a new line after line `i` starts with."""
        j = i
        while j > 0 and not self.lines[j].strip():
            j -= 1
        line = self.lines[j]
        indent = len(line) - len(line.lstrip(" "))
        if code_part(line).rstrip().endswith(":"):
            indent += INDENT
        return indent

    def _finish(self):
        """The block ends: its text without trailing blank lines, which the
        history keeps."""
        self._to_end()
        self.write("\n")
        lines = list(self.lines)
        while len(lines) > 1 and not lines[-1].strip():
            lines.pop()
        text = "\n".join(lines)
        self.history.add(text)
        return ("run", text)

    def _enter(self):
        first = self.lines[0]
        if len(self.lines) == 1 and self.routed is not None and self.routed(first):
            return self._finish()
        if is_compound(first):
            last = self.lines[-1]
            if len(self.lines) >= 3 and not last.strip() and not self.lines[-2].strip():
                return self._finish()
        elif not open_brackets(self.text()):
            return self._finish()
        # One more line, at the end of the block.
        indent = self._indent_after(len(self.lines) - 1)
        self.lines.append(" " * indent)
        self.row = len(self.lines) - 1
        self.col = indent
        self.redraw()
        return None

    def feed(self, ch, key, mods):
        """One key press: what to do, or None to go on."""
        ctrl = mods & keys.CTRL
        if key in (keys.ENTER, keys.KP_ENTER):
            if ctrl:
                return self._finish()
            return self._enter()
        if key == keys.F1:
            return ("key", "F1")
        if key == keys.F2:
            return ("key", "F2")
        if ctrl and ch:
            c = ch.lower()
            if c == "d":
                if not self.text().strip():
                    self.write("\n")
                    return ("eof",)
                self._delete()
            elif c == "a":
                self.col = 0
            elif c == "e":
                self.col = len(self.lines[self.row])
            elif c == "k":
                self.lines[self.row] = self.lines[self.row][:self.col]
            elif c == "u":
                self.lines[self.row] = self.lines[self.row][self.col:]
                self.col = 0
            else:
                return None
        elif key == keys.BACKSPACE:
            self._backspace()
        elif key == keys.DELETE:
            self._delete()
        elif key == keys.LEFT:
            self._left()
        elif key == keys.RIGHT:
            self._right()
        elif key == keys.HOME:
            self.col = 0
        elif key == keys.END:
            self.col = len(self.lines[self.row])
        elif key == keys.UP:
            self._vertical(-1)
        elif key == keys.DOWN:
            self._vertical(1)
        elif key == keys.PAGE_UP:
            self._history(-1)
        elif key == keys.PAGE_DOWN:
            self._history(1)
        elif key == keys.TAB:
            self._insert(" " * (INDENT - self.col % INDENT))
        elif ch and ch >= " " and not mods & (keys.ALT | keys.META):
            self._insert(ch)
        else:
            return None  # ESC and the keys that type nothing
        self.redraw()
        return None

    def cancel(self):
        """Ctrl+C at the prompt: the block is dropped, a new prompt comes."""
        self._to_end()
        self.write("\n")
