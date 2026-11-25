# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's editor and dispatcher (user/koracli) on the unix port: key events
# fed to the editor, what it draws and when a block runs, and where a block
# goes. tests/run-micropython-tests.sh puts user/koracli on the module path.

import dispatch
import editor
import keys

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


class Screen:
    def __init__(self):
        self.out = []

    def write(self, s):
        self.out.append(s)

    def take(self):
        s = "".join(self.out)
        self.out = []
        return s


def new(width=40, routed=dispatch.routed):
    screen = Screen()
    ed = editor.Editor(screen.write, width)
    ed.start(">>> ", "... ", routed)
    return ed, screen


def press(ed, key, mods=0, ch=""):
    return ed.feed(ch, key, mods)


def type_text(ed, text):
    """Type `text`; '\n' is Enter. What the last key asked for."""
    action = None
    for c in text:
        if c == "\n":
            action = press(ed, keys.ENTER)
        else:
            action = press(ed, 0x04, 0, c)
    return action


# A line that is complete runs at Enter.
ed, screen = new()
check(screen.take() == ">>> ", "the prompt")
check(type_text(ed, "x = 5\n") == ("run", "x = 5"), "a simple line runs")

# A compound statement: one blank line is kept, the second ends the block.
ed, screen = new()
check(type_text(ed, "def f(x):\n") is None and ed.lines == ["def f(x):", "    "],
      "a block goes on, indented")
check(type_text(ed, "a = x\n") is None, "its next line")
check(type_text(ed, "\n") is None and len(ed.lines) == 4, "one blank line is kept")
check(type_text(ed, "return a * 2\n\n") is None, "more after the blank line")
action = type_text(ed, "\n")
check(action == ("run", "def f(x):\n    a = x\n    \n    return a * 2"),
      "two blank lines end it, without them: %r" % (action,))
ns = {}
exec(dispatch.compile_block(action[1]), ns)
check(ns["f"](21) == 42, "and the block runs")

# Open brackets go on until they close; an indented line after a colon.
ed, screen = new()
check(type_text(ed, "x = [1,\n") is None, "an open bracket goes on")
check(type_text(ed, "2]\n") == ("run", "x = [1,\n2]"), "and runs once it closes")
ed, screen = new()
check(type_text(ed, "s = '''a\n") is None, "an open triple-quoted string goes on")
check(type_text(ed, "b'''\n") == ("run", "s = '''a\nb'''"), "and runs once it closes")
check(not editor.open_brackets("x = '(' # ("), "brackets in a string or a comment do not count")

# Ctrl+Enter runs a block at once.
ed, screen = new()
type_text(ed, "for i in range(2):\n")
type_text(ed, "print(i)")
check(press(ed, keys.ENTER, keys.CTRL & 0x01) == ("run", "for i in range(2):\n    print(i)"),
      "Ctrl+Enter runs the block as it is")

# Prefixed first lines run at Enter, never a block.
ed, screen = new()
check(type_text(ed, "10 FOR I = 1 TO 3:\n") == ("run", "10 FOR I = 1 TO 3:"),
      "a program line runs at Enter, even ending in a colon")
ed, screen = new()
check(type_text(ed, "!ls (\n") == ("run", "!ls ("), "an OS command too")

# Editing.
ed, screen = new()
type_text(ed, "helo")
press(ed, keys.LEFT)
type_text(ed, "l")
check(ed.lines == ["hello"] and ed.col == 4, "insert at the cursor")
press(ed, keys.HOME)
press(ed, keys.DELETE)
press(ed, keys.END)
press(ed, keys.BACKSPACE)
check(ed.lines == ["ell"], "Home, Delete, End, Backspace")
press(ed, keys.LEFT)
press(ed, 0x0E, 0x01, "k")
check(ed.lines == ["el"], "Ctrl+K")
press(ed, 0x18, 0x01, "u")
check(ed.lines == [""] and ed.col == 0, "Ctrl+U")
press(ed, keys.TAB)
check(ed.lines == ["    "], "Tab: four spaces")
check(press(ed, 0x29) is None and ed.lines == ["    "], "ESC types nothing at the prompt")
check(press(ed, 0x04, 0x04, "a") is None and ed.lines == ["    "], "Alt+A types nothing")

# Lines of a block: Backspace at a line's start joins it to the one before.
ed, screen = new()
type_text(ed, "if x:\n")
press(ed, keys.HOME)
press(ed, keys.BACKSPACE)
check(ed.lines == ["if x:    "] and ed.row == 0, "Backspace joins lines")
press(ed, keys.END)
press(ed, keys.RIGHT)
check(ed.row == 0, "Right stops at the block's end")

# History: blocks, by PgUp and PgDn, and Up past the first line.
hist = editor.History()
screen = Screen()
ed = editor.Editor(screen.write, 40, hist)
ed.start(">>> ", "... ", dispatch.routed)
type_text(ed, "a = 1\n")
ed.start(">>> ", "... ", dispatch.routed)
type_text(ed, "def g():\nreturn 2\n\n\n")
ed.start(">>> ", "... ", dispatch.routed)
type_text(ed, "draft")
press(ed, keys.PAGE_UP)
check(ed.lines == ["def g():", "    return 2"], "PgUp: the last block, whole")
press(ed, keys.UP)
check(ed.row == 0 and ed.lines[0] == "def g():", "Up moves within the block")
press(ed, keys.UP)
check(ed.lines == ["a = 1"], "and past its first line, the block before")
press(ed, keys.PAGE_DOWN)
press(ed, keys.PAGE_DOWN)
check(ed.lines == ["draft"], "PgDn back to the draft")
check(len(hist.blocks) == 2, "the history keeps the blocks run")

# Ctrl+D, F1 and F2.
ed, screen = new()
check(press(ed, 0x07, 0x01, "d") == ("eof",), "Ctrl+D on an empty prompt ends")
ed, screen = new()
type_text(ed, "ab")
press(ed, keys.HOME)
check(press(ed, 0x07, 0x01, "d") is None and ed.lines == ["b"], "elsewhere it deletes")
check(press(ed, keys.F1) == ("key", "F1") and press(ed, keys.F2) == ("key", "F2"), "F1, F2")

# Drawing: a wrapped line, and the cursor brought back to its place.
ed, screen = new(width=10)
screen.take()
type_text(ed, "abcdefghij")  # ">>> " and 10 characters: two rows
out = screen.take()
check(out.endswith("\r\x1b[J>>> abcdefghij\r\x1b[4C"), "the cursor after a wrapped line: %r"
      % out[-30:])
press(ed, keys.HOME)
out = screen.take()
check(out == "\x1b[1A\r\x1b[J>>> abcdefghij\x1b[1A\r\x1b[4C", "Home goes back up a row: %r" % out)

# The dispatcher.
check(dispatch.classify("print(1)", "PY") == (dispatch.PYTHON, "print(1)"), "Python")
check(dispatch.classify("10 PRINT X", "PY") == (dispatch.PROGRAM_LINE, "10 PRINT X"),
      "a program line")
check(dispatch.classify("!dir", "PY") == (dispatch.OS_COMMAND, "dir"), "! is an OS command")
check(dispatch.classify("%RUN", "PY") == (dispatch.OTHER_LANGUAGE, "RUN"), "% the other language")
check(dispatch.classify("print(1)", "OS") == (dispatch.OS_COMMAND, "print(1)"),
      "OS mode has no prefixes")
program = dispatch.Program()
program.enter("20 PRINT \"B\"")
program.enter("10 PRINT \"A\"")
check(program.lines == {10: "PRINT \"A\"", 20: "PRINT \"B\""}, "program lines stored")
program.enter("10")
check(program.lines == {20: "PRINT \"B\""}, "a number alone deletes its line")
check(editor.is_compound("  for x in y:") and editor.is_compound("@deco") and
      not editor.is_compound("format = 1"), "compound statements")

# A block's value is printed, as at MicroPython's prompt.
exec(dispatch.compile_block("6 * 7"), {})
exec(dispatch.compile_block("a = 1\nb = 2\nprint(a + b)"), {})

print("koracli editor: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
