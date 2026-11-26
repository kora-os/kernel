# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's input dispatch (project_doc/koracli-spec.md §4): where a block
# typed at the prompt goes. In Python and BASIC modes the first character of
# the line typed at the prompt decides: a digit, a BASIC program line; "!",
# an OS command; "%", the other language for this line; anything else, the
# mode's language. In OS mode every line is an OS command. Lines inside a
# Python block are Python.
#
# `!` lines are Python's in both modes: bang.py turns them into calls.
#
# The mode keys: F3 is Python and F4 BASIC, F2 goes to OS mode and back to
# the language it came from.

PYTHON = "python"
BASIC = "basic"
PROGRAM_LINE = "program line"
OS_COMMAND = "os"


MODE_KEYS = {"F3": "PY", "F4": "BAS"}


def switch(mode, language, key):
    """(mode, language) after the mode key `key` ("F2", "F3" or "F4"), in
    `mode` with `language` the last language mode ("PY" or "BAS")."""
    if key == "F2":
        return (language if mode == "OS" else "OS"), language
    mode = MODE_KEYS[key]
    return mode, mode


def routed(line):
    """Whether a first line goes somewhere else whole, by its prefix: it runs
    at Enter, never starting a block."""
    return line[:1].isdigit() or line[:1] in ("!", "%")


def classify(text, mode):
    """(kind, rest) for a block typed in `mode` ("PY", "BAS" or "OS")."""
    if mode == "OS":
        return OS_COMMAND, text
    first = text[:1]
    if first.isdigit():
        return PROGRAM_LINE, text
    if first == "!":
        return OS_COMMAND, text[1:]
    if first == "%":
        return (BASIC if mode == "PY" else PYTHON), text[1:]
    return (PYTHON if mode == "PY" else BASIC), text


def compile_block(text):
    """A block's code, compiled as the REPL compiles a line, so that an
    expression's value is printed; several statements (from Ctrl+Enter) as
    a module."""
    try:
        return compile(text, "<stdin>", "single")
    except SyntaxError:
        return compile(text, "<stdin>", "exec")
