# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's shell loop: the bars, the editor on key events, the mode keys
# (F3 Python, F4 BASIC, F2 OS mode and back), and the blocks run. Python
# runs in a namespace of its own, which BASIC's interpreter (classic.py,
# loaded when first used) shares its variables with; both have ESC as a
# break (KeyboardInterrupt).
# The console stays in key-event mode, and input() reads the keys as bytes.

import sys

import kora

import bang
import commands
import dispatch
import editor
import keys

# Classic BASIC's prompt is READY., printed after each command: the line
# typed has none.
PROMPTS = {"PY": (">>> ", "... "), "BAS": ("", ""), "OS": ("$ ", "$ ")}

HELP = """KoraCLI: Python at the prompt.
  Enter runs a line; a block (def, for, if...) runs at the second blank line,
  or at once with Ctrl+Enter. PgUp and PgDn bring back earlier blocks.
  ESC or Ctrl+C stops running code; Ctrl+C stops a program.
  Ctrl+D on an empty prompt ends KoraCLI.
  !cmd args runs a program, name = !cmd args keeps its output (a str with
  .status, .lines, .stderr); {expr} puts a Python value in; < > >> redirect;
  & runs it in the background, @N on core N. kora.run() and kora.capture()
  do the same in code.
  F3 switches to Python (PY), F4 to BASIC (BAS); F2 to OS commands (OS)
  and back to the language you were in.
  A line starting with a number is a BASIC program line, in every mode but
  OS; RUN runs the program, LIST lists it, NEW clears it, ESC stops it and
  CONT goes on. % runs a line of the other language: %RUN in Python mode,
  %print(x) in BASIC mode. Python and BASIC share variables: msg$ is
  Python's msg, and SYS "name", args calls a Python function."""


def write(s):
    print(s, end="")


def bars(mode):
    kora.bar(kora.TOP, " KoraCLI")
    kora.bar(kora.BOTTOM, " %s | F1 Help  F2 %s  F3 PY  F4 BAS" %
             (mode, "Back" if mode == "OS" else "OS"))


def prompts(ed, mode, start=True):
    prompt, more = PROMPTS[mode]
    # Only Python has blocks: in BASIC and OS mode every line runs at Enter.
    routed = dispatch.routed if mode == "PY" else (lambda line: True)
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


def basic(state):
    """Classic BASIC's interpreter, made when BASIC is first used."""
    b = state.get("basic")
    if b is None:
        import classic

        def readline():
            try:
                return input()
            except EOFError:
                return ""

        def getkey():
            k = kora.key(False)
            return "" if k is None else classic.key_char(k[0], k[1])

        def command(name, args):
            """SYS's OS commands and wrappers; False if there is none."""
            try:
                kora.run(name, *args)
            except OSError:
                return False
            return True

        # BASIC's variables are Python's too: the namespace Python runs in.
        b = classic.Interpreter(write, readline, getkey, state["width"], state["ns"], command)
        state["basic"] = b
    return b


def run_basic(text, state):
    b = basic(state)
    kora.esc_break(True)
    try:
        b.execute(text)
    except KeyboardInterrupt:
        print("BREAK")
    except Exception as e:  # a mistake in the interpreter, not the program
        sys.print_exception(e)
    finally:
        kora.esc_break(False)


def run(text, mode, ns, state):
    kind, rest = dispatch.classify(text, mode)
    if kind == dispatch.PROGRAM_LINE:
        basic(state).enter(rest)
    elif mode == "OS":
        if text.strip():
            kora.esc_break(False)
            run_os(text)
    elif kind == dispatch.BASIC:
        if rest.strip():
            run_basic(rest, state)
        if mode == "BAS" and text.strip():
            print("READY.")
    elif text.strip():
        # Python (a ! line too), its ! lines made calls of kora's (bang.py).
        code = text if kind == dispatch.OS_COMMAND or mode == "PY" else rest
        try:
            code = bang.translate(code)
        except bang.CommandError as e:
            print("koracli: %s" % e)
            return
        run_python(code, ns)
        if mode == "BAS":
            print("READY.")


def report_jobs():
    """Background jobs that ended, as the shell before reported them."""
    for pid, code in kora.reap():
        print("[%d] done, exit %d" % (pid, code))


def read_action(ed, state):
    """One round at the prompt: the jobs that ended, a prompt, keys until
    there is something to do (the mode keys switch mode on the way)."""
    report_jobs()
    prompts(ed, state["mode"])
    while True:
        action = ed.feed(*kora.key())
        if action is None:
            continue
        if action[0] == "key" and action[1] != "F1":
            mode, state["language"] = dispatch.switch(state["mode"], state["language"],
                                                      action[1])
            if mode == state["mode"]:
                continue  # F3 in Python mode, F4 in BASIC mode
            state["mode"] = mode
            bars(mode)
            if mode == "BAS" and not ed.text():
                # On an empty line, BASIC says it is ready, as a C64 does.
                write("\r\x1b[KREADY.\n")
                prompts(ed, mode)
            else:
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
    ed = editor.Editor(write, width)
    commands.register()
    bars("PY")
    try:
        version = " " + ".".join(str(n) for n in sys.implementation.version[:3])
    except AttributeError:
        version = ""
    print("KoraCLI on MicroPython%s. F1 for help." % version)
    kora.mode(kora.EVENTS)
    state = {"mode": "PY", "language": "PY", "width": width, "ns": ns}
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
                    run(action[1], state["mode"], ns, state)
                except KeyboardInterrupt:  # a break that came late
                    try:
                        print("KeyboardInterrupt")
                    except KeyboardInterrupt:
                        pass
    finally:
        kora.mode(kora.COOKED)
        kora.bar(kora.TOP, None)
        kora.bar(kora.BOTTOM, None)
