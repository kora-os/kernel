# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's shell loop (KoraCLI step 1b): the bars, the editor on key
# events, F2 between Python and OS mode, and the blocks run. Python runs in
# a namespace of its own with ESC as a break (KeyboardInterrupt); the console
# stays in key-event mode, and input() reads the keys as bytes.

import sys

import kora

import dispatch
import editor
import keys

MODES = ("PY", "OS")
PROMPTS = {"PY": (">>> ", "... "), "OS": ("$ ", "$ ")}

HELP = """KoraCLI: Python at the prompt.
  Enter runs a line; a block (def, for, if...) runs at the second blank line,
  or at once with Ctrl+Enter. PgUp and PgDn bring back earlier blocks.
  ESC or Ctrl+C stops running code. Ctrl+D on an empty prompt ends KoraCLI.
  F2 switches between Python (PY) and OS commands (OS).
  A line starting with a number is a BASIC program line (kept for later),
  ! runs an OS command and % a line of BASIC: those come in later steps."""


def write(s):
    print(s, end="")


def bars(mode):
    kora.bar(kora.TOP, " KoraCLI")
    kora.bar(kora.BOTTOM, " %s | F1 Help  F2 Mode" % mode)


def prompts(ed, mode, start=True):
    prompt, more = PROMPTS[mode]
    routed = (lambda line: True) if mode == "OS" else dispatch.routed
    if start:
        ed.start(prompt, more, routed)
    else:
        ed.set_prompts(prompt, more, routed)


def run_python(text, ns):
    # The console stays in EVENTS mode: input() gets the keys as bytes (the
    # HAL's), and keys typed ahead wait for the editor.
    kora.esc_break(True)
    try:
        exec(dispatch.compile_block(text), ns)
    except KeyboardInterrupt:
        print("KeyboardInterrupt")
    except SystemExit:
        raise
    except BaseException as e:
        sys.print_exception(e)
    finally:
        kora.esc_break(False)


def run(text, mode, ns, program):
    kind, rest = dispatch.classify(text, mode)
    if kind == dispatch.PYTHON:
        if text.strip():
            run_python(text, ns)
    elif kind == dispatch.PROGRAM_LINE:
        program.enter(rest)
    elif kind == dispatch.OS_COMMAND:
        if rest.strip():
            print("koracli: OS commands come with KoraCLI step 1c")
    else:
        print("koracli: BASIC comes with KoraCLI phase 2")


def main():
    size = kora.size()
    width = size[0] if size else 80
    mode = "PY"
    ns = {"__name__": "__main__", "kora": kora}
    program = dispatch.Program()
    ed = editor.Editor(write, width)
    bars(mode)
    try:
        version = " " + ".".join(str(n) for n in sys.implementation.version[:3])
    except AttributeError:
        version = ""
    print("KoraCLI on MicroPython%s. F1 for help." % version)
    kora.mode(kora.EVENTS)
    try:
        while True:
            prompts(ed, mode)
            while True:
                try:
                    action = ed.feed(*kora.key())
                except KeyboardInterrupt:  # Ctrl+C at the prompt
                    ed.cancel()
                    prompts(ed, mode)
                    continue
                if action is None:
                    continue
                if action[0] == "key" and action[1] == "F2":
                    mode = MODES[(MODES.index(mode) + 1) % len(MODES)]
                    bars(mode)
                    prompts(ed, mode, start=False)
                    continue
                if action[0] == "key":  # F1
                    ed.cancel()
                    print(HELP)
                    break
                break
            if action[0] == "eof":
                return
            if action[0] == "run":
                try:
                    run(action[1], mode, ns, program)
                except KeyboardInterrupt:  # a break that came late
                    print("KeyboardInterrupt")
    finally:
        kora.mode(kora.COOKED)
        kora.bar(kora.TOP, None)
        kora.bar(kora.BOTTOM, None)
