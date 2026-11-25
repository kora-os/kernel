# SPDX-License-Identifier: GPL-3.0-or-later
#
# The `kora` module: KoraOS from Python. Its C part is `_kora`
# (user/micropython/modkora.c: the system, the console, running programs);
# this adds what is better written in Python (KoraCLI step 1c):
#
#   run(cmd, *args, ...)      run a program, its output on the console: its
#                             exit status (also in kora.status)
#   capture(cmd, *args, ...)  run it, its output kept: a str with .status,
#                             .lines and .stderr
#   status                    the last exit status
#   cmd                       wrappers: @kora.cmd.register makes a Python
#                             function the command of its name
#
# Both take background=True (start it and return its pid; its end is
# reported later), core=N (the core it runs on), stdin="file",
# stdout="file" and append=True. `cmd` is found as a wrapper first, then as
# a program in c:; a path (with / or :) is always the program.

try:
    from _kora import *
    import _kora
except ImportError:  # on the host (tests/micropython): no system to run on
    _kora = None

status = 0


class Captured(str):
    """What capture() returns: the output, a str, with the exit status
    (.status), its lines (.lines) and the error output (.stderr)."""


def _captured(text, code, stderr):
    result = Captured(text)
    result.status = code
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    result.lines = lines
    result.stderr = stderr
    return result


class Commands:
    """kora.cmd: the wrappers, by name (kora.cmd.dir, kora.cmd["dir"])."""

    def __init__(self):
        self._table = {}

    def register(self, fn=None, name=None):
        """@kora.cmd.register, or kora.cmd.register(fn, "name"): the function
        is the command `name` (its own name by default) from now on."""
        if fn is None:
            return lambda f: self.register(f, name)
        name = name or fn.__name__
        self._table[name] = fn
        setattr(self, name, fn)
        return fn

    def unregister(self, name):
        del self._table[name]
        delattr(self, name)

    def find(self, name):
        """The wrapper for `name`, or None (always None for a path)."""
        if "/" in name or ":" in name:
            return None
        return self._table.get(name)

    def __getitem__(self, name):
        return self._table[name]

    def names(self):
        return sorted(self._table)


cmd = Commands()


def _execute(name, args, capture, background, core, stdin, stdout, append):
    return _kora.execute([name] + [str(a) for a in args], stdin=stdin, stdout=stdout,
                         append=append, core=-1 if core is None else core,
                         background=background, capture=capture)


def run(name, *args, background=False, core=None, stdin=None, stdout=None, append=False):
    """Run `name`: a wrapper's result, or the program's exit status (its pid
    with background=True)."""
    global status
    wrapper = cmd.find(name)
    if wrapper is not None:
        return wrapper(*args)
    code = _execute(name, args, False, background, core, stdin, stdout, append)
    if not background:
        status = code
    return code


def capture(name, *args, background=False, core=None, stdin=None, stdout=None, append=False):
    """Run `name` with its output kept: a wrapper's result, or a Captured
    (a pid with background=True, the output then shown)."""
    global status
    wrapper = cmd.find(name)
    if wrapper is not None:
        return wrapper(*args)
    if background:
        return _execute(name, args, False, True, core, stdin, stdout, append)
    code, out, err = _execute(name, args, True, False, core, stdin, None, False)
    status = code
    return _captured(out, code, err)


# What KoraCLI's `!` lines become (sys:koracli/bang.py): the command's words
# as pieces, literal text and (value,) for {expr}, put together here.

def _word(pieces):
    """One word: its pieces joined, a value as str(value)."""
    parts = []
    for p in pieces:
        if isinstance(p, tuple):
            if isinstance(p[0], (list, tuple)):
                raise TypeError("a list in {} can only stand alone as an argument")
            parts.append(str(p[0]))
        else:
            parts.append(p)
    return "".join(parts)


def _argv(words):
    """The arguments of `words`: a lone {list} gives one argument per item."""
    argv = []
    for pieces in words:
        if len(pieces) == 1 and isinstance(pieces[0], tuple) and \
                isinstance(pieces[0][0], (list, tuple)):
            argv.extend(str(v) for v in pieces[0][0])
        else:
            argv.append(_word(pieces))
    return argv


def _bang(capturing, words, stdin=None, stdout=None, append=False, background=False,
          core=None):
    argv = _argv(words)
    if not argv:
        raise ValueError("no command")
    stdin = _word(stdin) if stdin is not None else None
    stdout = _word(stdout) if stdout is not None else None
    try:
        if capturing:
            return capture(argv[0], *argv[1:], background=background, core=core,
                           stdin=stdin, stdout=stdout, append=append)
        result = run(argv[0], *argv[1:], background=background, core=core, stdin=stdin,
                     stdout=stdout, append=append)
    except OSError:
        print("%s: no such program, or it cannot run so" % argv[0])
        return None
    if capturing:
        return result
    if background:
        print("[%d]" % result)
    elif cmd.find(argv[0]) is not None and result is not None:
        print(result)  # a wrapper's result is shown; a program's status is not
    return None
