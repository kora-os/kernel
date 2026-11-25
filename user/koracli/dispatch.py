# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's input dispatch (project_doc/koracli-spec.md §4): where a block
# typed at the prompt goes. In Python mode the first character of the line
# typed at ">>>" decides: a digit, a BASIC program line; "!", an OS command;
# "%", the other language for this line; anything else, Python. In OS mode
# every line is an OS command. Lines inside a block are Python.
#
# For now (KoraCLI step 1b) a program line is stored (nothing lists or runs
# it yet), and OS commands and BASIC say when they come.

PYTHON = "python"
PROGRAM_LINE = "program line"
OS_COMMAND = "os"
OTHER_LANGUAGE = "other"


def routed(line):
    """Whether a first line goes somewhere else whole, by its prefix: it runs
    at Enter, never starting a block."""
    return line[:1].isdigit() or line[:1] in ("!", "%")


def classify(text, mode):
    """(kind, rest) for a block typed in `mode` ("PY" or "OS")."""
    if mode == "OS":
        return OS_COMMAND, text
    first = text[:1]
    if first.isdigit():
        return PROGRAM_LINE, text
    if first == "!":
        return OS_COMMAND, text[1:]
    if first == "%":
        return OTHER_LANGUAGE, text[1:]
    return PYTHON, text


class Program:
    """The BASIC program's lines, by number: stored as typed, unchecked."""

    def __init__(self):
        self.lines = {}

    def enter(self, text):
        """A program line: "10 PRINT X" stores line 10, "10" alone deletes
        it. The number and the line, or None for a line that is not one."""
        i = 0
        while i < len(text) and text[i].isdigit():
            i += 1
        number = int(text[:i])
        rest = text[i:].strip()
        if rest:
            self.lines[number] = rest
        elif number in self.lines:
            del self.lines[number]
        return number, rest


def compile_block(text):
    """A block's code, compiled as the REPL compiles a line, so that an
    expression's value is printed; several statements (from Ctrl+Enter) as
    a module."""
    try:
        return compile(text, "<stdin>", "single")
    except SyntaxError:
        return compile(text, "<stdin>", "exec")
