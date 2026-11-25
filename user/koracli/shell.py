# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's shell loop (KoraCLI step 1b): the bars, the editor on key
# events, F2 between Python and OS mode, and the blocks run. Python runs in
# a namespace of its own with ESC as a break (KeyboardInterrupt); the console
# stays in key-event mode, and input() reads the keys as bytes.

import sys

import kora

import bang
import commands
import dispatch
import editor
import keys

MODES = ("PY", "OS")
PROMPTS = {"PY": (">>> ", "... "), "OS": ("$ ", "$ ")}

HELP = """KoraCLI: Python at the prompt.
  Enter runs a line; a block (def, for, if...) runs at the second blank line,
  or at once with Ctrl+Enter. PgUp and PgDn bring back earlier blocks.
  ESC or Ctrl+C stops running code; Ctrl+C stops a program.
  Ctrl+D on an empty prompt ends KoraCLI.
  !cmd args runs a program, name = !cmd args keeps its output (a str with
  .status, .lines, .stderr); {expr} puts a Python value in; < > >> redirect;
  & runs it in the background, @N on core N. kora.run() and kora.capture()
  do the same in code.
  F2 switches between Python (PY) and OS commands (OS).
  A line starting with a number is a BASIC program line (kept for later);
  % runs a line of BASIC, which comes in a later step."""


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


def run_os(text):
    """An OS mode line: a command, without {expr}, its end reported as the
    shell before reported it; a volume or assign name alone changes there."""
    try:
        c = bang.os_line(text)
    except bang.CommandError as e:
        print("koracli: %s" % e)
        return
    words = [tuple(w) for w in c["words"]]
    if len(words) == 1 and len(words[0][0]) > 1 and words[0][0].endswith(":") and \
            c["stdin"] is None and c["stdout"] is None and not c["background"] and \
            c["core"] is None:
        kora.run("cd", words[0][0])
        return
    kora._bang(False, words, stdin=tuple(c["stdin"]) if c["stdin"] else None,
               stdout=tuple(c["stdout"]) if c["stdout"] else None, append=c["append"],
               background=c["background"], core=c["core"], report=True)


def run(text, mode, ns, program):
    kind, rest = dispatch.classify(text, mode)
    if kind == dispatch.PROGRAM_LINE:
        program.enter(rest)
    elif kind == dispatch.OTHER_LANGUAGE:
        print("koracli: BASIC comes with KoraCLI phase 2")
    elif mode == "OS":
        if text.strip():
            kora.esc_break(False)
            run_os(text)
    elif text.strip():
        # Python, its ! lines made calls of kora's (bang.py).
        try:
            code = bang.translate(text)
        except bang.CommandError as e:
            print("koracli: %s" % e)
            return
        run_python(code, ns)


def report_jobs():
    """Background jobs that ended, as the shell before reported them."""
    for pid, code in kora.reap():
        print("[%d] done, exit %d" % (pid, code))


def read_action(ed, state):
    """One round at the prompt: the jobs that ended, a prompt, keys until
    there is something to do (F2 switches mode on the way)."""
    report_jobs()
    prompts(ed, state["mode"])
    while True:
        action = ed.feed(*kora.key())
        if action is None:
            continue
        if action[0] == "key" and action[1] == "F2":
            mode = MODES[(MODES.index(state["mode"]) + 1) % len(MODES)]
            state["mode"] = mode
            bars(mode)
            prompts(ed, mode, start=False)
            continue
        if action[0] == "key":  # F1
            ed.cancel(mark=False)
            print(HELP)
            return ("help",)
        return action


def drop_line(ed):
    """Ctrl+C at the prompt: the line is dropped (^C), even if another Ctrl+C
    comes meanwhile."""
    while True:
        try:
            ed.cancel()
            return
        except KeyboardInterrupt:
            pass


def main():
    size = kora.size()
    width = size[0] if size else 80
    ns = {"__name__": "__main__", "kora": kora}
    program = dispatch.Program()
    ed = editor.Editor(write, width)
    commands.register()
    bars("PY")
    try:
        version = " " + ".".join(str(n) for n in sys.implementation.version[:3])
    except AttributeError:
        version = ""
    print("KoraCLI on MicroPython%s. F1 for help." % version)
    kora.mode(kora.EVENTS)
    state = {"mode": "PY"}
    try:
        while True:
            # A Ctrl+C may come at any moment, also while the editor works on
            # a key or a first Ctrl+C is being handled: anywhere in a round
            # it drops the line and starts a new one.
            try:
                action = read_action(ed, state)
            except KeyboardInterrupt:
                drop_line(ed)
                continue
            if action[0] == "eof":
                print("bye")
                return
            if action[0] == "run":
                try:
                    run(action[1], state["mode"], ns, program)
                except KeyboardInterrupt:  # a break that came late
                    try:
                        print("KeyboardInterrupt")
                    except KeyboardInterrupt:
                        pass
    finally:
        kora.mode(kora.COOKED)
        kora.bar(kora.TOP, None)
        kora.bar(kora.BOTTOM, None)
