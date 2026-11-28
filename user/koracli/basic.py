# SPDX-License-Identifier: GPL-3.0-or-later
#
# What KoraCLI's BASIC dialects share (project_doc/koracli-spec.md §6): the
# program store, BASIC's errors, the output with its column (for PRINT's
# zones and TAB), the random numbers, and the runtime of a BASIC with line
# numbers (`Runtime`): the run of a program's lines compiled to closures,
# FOR, GOSUB and WHILE, DATA, CONT, the variables shared with Python (spec
# §7), and the commands that work on the program (RUN, LIST, NEW, SAVE,
# LOAD, RENUM, AUTO). The dialects are modules of their own, over it:
# classic.py, and BBC BASIC in phase 3.
#
# Errors are raised by their Classic BASIC names ("TYPE MISMATCH", "UNDEF'D
# STATEMENT"): the code both dialects share raises those, and a dialect's
# `report` words them its own way.

try:
    import os
except ImportError:
    os = None

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


class Halted(Exception):
    """A program halted (END, STOP, NEW) inside a call that runs statements
    of its own (BBC BASIC's PROC and FN): out to the run."""


def as_error(exc):
    """A Python exception a program caused, as BASIC's error; None for one
    that is not the program's (a mistake of the interpreter's)."""
    if isinstance(exc, BasicError):
        return exc
    if isinstance(exc, ZeroDivisionError):
        return BasicError("DIVISION BY ZERO")
    if isinstance(exc, OverflowError):
        return BasicError("OVERFLOW")
    if isinstance(exc, MemoryError):
        return BasicError("OUT OF MEMORY")
    if isinstance(exc, RuntimeError) and "recursion" in str(exc):
        return BasicError("OUT OF MEMORY")  # recursion too deep (FN calling itself)
    return None


# --- Values ---------------------------------------------------------------------------

SYNTAX = "SYNTAX"
AMBIGUOUS = "AMBIGUOUS NAME"
TYPE = "TYPE MISMATCH"
QUANTITY = "ILLEGAL QUANTITY"
UNDEF_LINE = "UNDEF'D STATEMENT"
UNDEF_FN = "UNDEF'D FUNCTION"

INF = float("inf")
MISSING = object()  # no value in the Python namespace


def to_int(v):
    """A number as an integer, toward zero, for % variables and the logical
    operators: 32 bits, signed."""
    if v != v or v >= 2147483648.0 or v < -2147483648.0:
        raise BasicError(QUANTITY)
    return int(v)


def finite(v):
    if v == INF or v == -INF or v != v:
        raise BasicError("OVERFLOW")
    return v


def nested(dims, default):
    """An array of `dims` (the highest index of each dimension): lists,
    nested for more than one dimension."""
    if len(dims) == 1:
        return [default] * (dims[0] + 1)
    return [nested(dims[1:], default) for _ in range(dims[0] + 1)]


def base_name(key):
    """A variable's name without its type suffix: what Python calls it."""
    return key[:-1] if key[-1:] in ("$", "%") else key


def from_python(suffix, v):
    """A value from Python for a BASIC variable with `suffix`: numbers
    convert (True is -1), strings are strings, anything else is a type
    mismatch."""
    t = type(v)
    if suffix == "$":
        if t is str:
            return v
        raise BasicError(TYPE)
    if t is float:
        n = v
    elif t is bool:
        n = -1.0 if v else 0.0
    elif t is int:
        n = v
    else:
        raise BasicError(TYPE)
    if suffix == "%":
        return to_int(n)
    return finite(float(n))


def default(name):
    return "" if name.endswith("$") else 0 if name.endswith("%") else 0.0


def converter(name):
    if name.endswith("$"):
        return None
    if name.endswith("%"):
        return to_int
    return lambda v: finite(float(v))


# --- Files ----------------------------------------------------------------------------
#
# Spec §12: plain text, `.BAS` when the name has no extension, with the
# dialect in a header that is the file's line 0. The header is the file's
# only: LOAD takes it away, so a program's own line 0 stays its own.

HEADER = "0 REM !KoraBasic:"
DIALECTS = ("Classic", "BBC")


class OtherDialect(Exception):
    """A file saved in the other dialect: out of the interpreter to
    KoraCLI, which asks before it switches (spec §6.3). `name` is the file's
    as LOAD was given it, `run` whether it was CHAIN (load, then run)."""

    def __init__(self, dialect):
        super().__init__(dialect)
        self.dialect = dialect
        self.name = None
        self.run = False


def file_path(name):
    """A program's file: `.BAS` added when the name has no extension."""
    if not name:
        raise BasicError("MISSING FILE NAME")
    base = name[max(name.rfind("/"), name.rfind(":")) + 1:]
    return name if "." in base else name + ".BAS"


def read_program(text, dialect, normalize=None):
    """A file's lines, by number, after its header (another dialect's is
    refused); blank lines are skipped, and a line without a number, or past
    63999, is the file's mistake."""
    program = Program(normalize)
    first = True
    for line in text.split("\n"):
        line = line.strip()
        if not line:
            continue
        if first:
            first = False
            if line.upper().startswith(HEADER.upper()):
                named = line[len(HEADER):].strip().upper()
                if named != dialect.upper():
                    for d in DIALECTS:
                        if d.upper() == named:
                            raise OtherDialect(d)
                    raise BasicError("UNKNOWN DIALECT")
                continue
        if not ("0" <= line[0] <= "9"):
            raise BasicError("FILE DATA")
        try:
            program.enter(line)
        except BasicError:
            raise BasicError("FILE DATA")
    return program.lines


# --- The runtime ----------------------------------------------------------------------

class Runtime:
    """A BASIC with line numbers: its program, variables and run. A dialect
    gives it, as methods: `compile_line(text)` (a line's statements as
    closures, and their keywords), `data_items(text)` (a line's DATA items,
    (text, quoted) each), `report(e, line)` and `report_break(line)` (an
    error and a break, in its words), `line_refs(text)` and
    `renumber_line(text, new)` (for RENUM), `warn_ref(target, line)`, and
    `num_text(x)` (a number as an OS command's argument); and DIALECT, its
    name in a file's header. `report_break` is told how the program
    stopped: "STOP" or "BREAK" (ESC). `announce(what, name)` says what SAVE
    and LOAD do ("save", "search", "load"), `unset(key)` is the value of a
    variable never written (Classic's 0 or "", BBC's error), and
    `undimmed(key, spelling, n)` the array used before a DIM: the dialect's
    choice.

    A statement's closure returns None to go on, or True when it moved the
    program (a jump or an end), having set the place: `li`, the line's
    index in the program (-1 for a line typed without a number), and `si`,
    the statement's."""

    DIALECT = None
    WHILE_END = "WEND"  # the keyword that ends a WHILE's body

    def __init__(self, write, readline=None, getkey=None, width=80, ns=None, command=None,
                 normalize=None, seed=1):
        self.program = Program(normalize)
        self.normalize = normalize
        self.out = Output(write, width)
        self.readline = readline
        self.getkey = getkey
        self.command = command
        self.ns = {} if ns is None else ns  # Python's namespace, shared
        self.exports = {}  # Python name: the object BASIC last put there
        self.names = {}  # BASIC's name without its suffix: Python's name
        self.created = set()  # Python names BASIC made (RUN takes them away)
        self.unresolved = set()  # names Python has none for, until it runs again
        self.vars = {}
        self.arrays = {}
        self.stack = []
        self.data = None
        self.data_pos = 0
        self.code = {}  # line number: (text, statements, keywords)
        self.order = []
        self.index = {}
        self.order_version = -1
        self.wends = {}  # a WHILE's place: the place after its end
        self.li = -1
        self.si = 0
        self.direct = ([], [])
        self.halted = None
        self.no_cont = False  # stopped inside a call: CONT cannot go back into it
        self.cont = None
        self.random = Random(seed)
        self.file = None  # the name last saved or loaded (F6 offers it)
        self.auto = None  # AUTO's [next line, step]: KoraCLI types the number

    def announce(self, what, name):
        pass

    def unset(self, key):
        return default(key)

    def undimmed(self, key, spelling, n):
        return self.dim(key, [10] * n, spelling)

    # The program.

    def enter(self, text):
        """A program line typed (from the dispatcher). Under AUTO, its
        number alone ends AUTO (the line stays), and any other line moves
        AUTO on past it."""
        auto = self.auto
        if auto is not None and text.strip() == str(auto[0]):
            self.auto = None
            return None
        try:
            number, rest = self.program.enter(text)
        except BasicError as e:
            self.report(e, None)
            return None
        if auto is not None:
            auto[0] = number + auto[1]
            if auto[0] > MAX_LINE:
                self.auto = None
        return number, rest

    def auto_text(self):
        """The line AUTO offers: its next number, and the line there if
        there is one (to change or keep); None without AUTO."""
        if self.auto is None:
            return None
        n = self.auto[0]
        line = self.program.lines.get(n)
        return "%d %s" % (n, line) if line is not None else "%d " % n

    def _sync(self):
        if self.order_version != self.program.version:
            self.order = self.program.numbers()
            self.index = {n: i for i, n in enumerate(self.order)}
            self.order_version = self.program.version
            self.wends = {}
            for n in list(self.code):
                if n not in self.program.lines:
                    del self.code[n]

    def compiled(self, li):
        """Line `li`'s statements and their keywords."""
        if li < 0:
            return self.direct
        number = self.order[li]
        text = self.program.lines[number]
        c = self.code.get(number)
        if c is not None and c[0] is text:
            return c[1], c[2]
        stmts, kinds = self.compile_line(text)
        self.code[number] = (text, stmts, kinds)
        return stmts, kinds

    # Running.

    def execute(self, text):
        """A line typed without a number: its statements run at once (and
        RUN, GOTO or CONT go on into the program)."""
        self._sync()
        # Python may have run since: the names BASIC found there are looked
        # for again (those BASIC made stay).
        self.unresolved.clear()
        names = self.names  # in place: compiled lines hold this dict
        for b in [b for b, py in names.items() if py not in self.created]:
            del names[b]
        try:
            self.direct = self.compile_line(text)
        except BasicError as e:
            self.report(e, None)
            return
        self.li = -1
        self.si = 0
        self._run()

    def _run(self):
        self.halted = None
        self.no_cont = False
        while True:
            try:
                if not self.steps():
                    return
                break  # halted
            except Halted:  # END, STOP or NEW inside a call (BBC's PROC, FN)
                break
            except KeyboardInterrupt:
                if self.trap(BasicError("ESCAPE")):
                    continue
                self.halted = "BREAK"
                break
            except Exception as exc:
                e = as_error(exc)
                if e is None:
                    raise
                if self.trap(e):
                    continue  # the program's own handler (BBC's ON ERROR) goes on
                self.ended()
                self.report(e, self.line())
                return
        how = self.halted
        if how == "END" or how == "NEW":
            self.cont = None
            return
        # STOP, or ESC: CONT goes on from the next statement (STOP) or the
        # one that was interrupted (ESC), unless that was inside a call.
        line = self.line()
        if line is not None:
            si = self.si + 1 if how == "STOP" else self.si
            self.cont = None if self.no_cont else (self.li, si, self.program.version)
        self.out.newline_if_needed()
        self.report_break(line, how)  # line None: a line typed without a number

    def steps(self):
        """The statements from the place: False at the end (the program's,
        or a typed line's), True when one halted the program."""
        li = self.li
        i = self.si
        stmts = self.compiled(li)[0]
        while True:
            n = len(stmts)
            while i < n:
                self.si = i
                if stmts[i]():
                    break
                i += 1
            else:
                i = -1
            if i >= 0:
                # Moved: a jump, a skip to the line's end, or an end.
                if self.halted is not None:
                    return True
                li = self.li
                i = self.si
                stmts = self.compiled(li)[0]
                if i < len(stmts):
                    continue
            # Past this line's end: the next line, if there is one.
            if li < 0:
                return False  # a line typed without a number: CONT still works
            if li + 1 >= len(self.order):
                self.cont = None
                return False
            li += 1
            self.li = li
            self.si = i = 0
            stmts = self.compiled(li)[0]

    def trap(self, e):
        """Whether the program handles error `e` itself (BBC's ON ERROR),
        having set the place to its handler; ESC comes as "ESCAPE"."""
        return False

    def line(self):
        return self.order[self.li] if self.li >= 0 else None

    def ended(self):
        """An error: the program cannot go on (CONT), though a mistake in a
        line typed without a number leaves it as it was."""
        if self.li >= 0:
            self.cont = None

    def halt(self, how):
        self.halted = how
        return True

    def goto(self, number):
        li = self.index.get(number)
        if li is None:
            raise BasicError(UNDEF_LINE)
        self.li = li
        self.si = 0
        return True

    # FOR, GOSUB and WHILE share one stack, as on a C64: frames are ("F",
    # variable, limit, step, line, statement), ("G", line, statement) and
    # ("W", line, statement).

    def push_for(self, key, limit, step):
        stack = self.stack
        for j in range(len(stack) - 1, -1, -1):
            f = stack[j]
            if f[0] == "G":
                break
            if f[0] == "F" and f[1] == key:
                del stack[j:]
                break
        stack.append(("F", key, limit, step, self.li, self.si + 1))

    def do_next(self, key):
        stack = self.stack
        j = len(stack) - 1
        while j >= 0:
            f = stack[j]
            if f[0] == "G":  # a FOR outside the subroutine is not this NEXT's
                j = -1
                break
            if f[0] == "F" and (key is None or f[1] == key):
                break
            j -= 1
        if j < 0:
            raise BasicError("NEXT WITHOUT FOR")
        _, k, limit, step, li, si = stack[j]
        del stack[j + 1:]
        v = finite(self.scalar_get(k) + step)
        self.scalar_set(k, v if not k.endswith("%") else to_int(v))
        if (step >= 0 and v <= limit) or (step < 0 and v >= limit):
            self.li = li
            self.si = si
            return True
        stack.pop()
        return None

    def push_gosub(self):
        self.stack.append(("G", self.li, self.si + 1))

    def do_return(self):
        stack = self.stack
        while stack:
            f = stack.pop()
            if f[0] == "G":
                self.li = f[1]
                self.si = f[2]
                return True
        raise BasicError("RETURN WITHOUT GOSUB")

    def do_while(self, cond):
        place = (self.li, self.si)
        after = self.wends.get(place) if self.li >= 0 else None
        if after is None:
            after = self._find_wend()
            if self.li >= 0:
                self.wends[place] = after
        if cond() != 0:
            self.stack.append(("W", self.li, self.si))
            return None
        self.li, self.si = after
        return True

    def _find_wend(self):
        """The place after this WHILE's end."""
        li, si = self.li, self.si + 1
        depth = 0
        end = self.WHILE_END
        while True:
            kinds = self.compiled(li)[1]
            while si < len(kinds):
                k = kinds[si]
                if k == "WHILE":
                    depth += 1
                elif k == end:
                    if depth == 0:
                        return li, si + 1
                    depth -= 1
                si += 1
            if li < 0 or li + 1 >= len(self.order):
                raise BasicError("WHILE WITHOUT WEND")
            li += 1
            si = 0

    def do_wend(self):
        stack = self.stack
        while stack:
            f = stack.pop()
            if f[0] == "W":
                self.li = f[1]
                self.si = f[2]
                return True
            if f[0] == "G":
                stack.append(f)
                break
        raise BasicError("WEND WITHOUT WHILE")

    # Variables, shared with Python (spec §7): every write of BASIC's also
    # goes into the namespace, under the name's spelling first used, and
    # BASIC remembers the object it put there. A read finds Python's value
    # newer when the namespace holds another object.

    def clear(self):
        """BASIC's variables gone, and from Python's namespace the names BASIC
        made (unless Python has changed them since): Python's own stay, with
        what BASIC last wrote there."""
        ns = self.ns
        exports = self.exports
        for py in self.created:
            v = exports.get(py, MISSING)
            if v is not MISSING and ns.get(py, MISSING) is v:
                del ns[py]
        exports.clear()
        self.created.clear()
        self.names.clear()
        self.unresolved.clear()
        self.vars.clear()
        self.arrays.clear()
        self.stack.clear()
        self.data = None
        self.data_pos = 0

    def scalar(self, key, spelling):
        """Variable `key`'s read and write, as closures for a compiled line:
        `spelling` is the name as typed (without its suffix), the Python
        name it makes if Python has none to share. A read takes Python's
        value when it is newer than BASIC's last write."""
        conv = converter(key)
        base = base_name(key)
        suffix = key[len(base):]
        variables = self.vars
        ns = self.ns
        exports = self.exports
        names = self.names
        unset = self.unset

        def get():
            py = names.get(base)
            if py is None:
                py = self.resolve(base, spelling)
            if py is not None:
                v = ns.get(py, MISSING)
                if v is not MISSING and v is not exports.get(py, MISSING):
                    v = from_python(suffix, v)  # Python's, newer
                    variables[key] = v
                    return v
            v = variables.get(key, MISSING)
            return v if v is not MISSING else unset(key)

        def set_(v):
            if conv is not None:
                v = conv(v)
            variables[key] = v
            py = names.get(base)
            if py is None:
                py = self.name_for(base, spelling)
            ns[py] = v
            exports[py] = v
        return get, set_

    def resolve(self, base, spelling):
        """The Python name BASIC's `base` reads, or None: the same spelling,
        else the one name that matches without regard to case."""
        if base in self.unresolved:
            return None
        ns = self.ns
        if spelling in ns:
            py = spelling
        else:
            found = [k for k in ns if type(k) is str and k.upper() == base]
            if len(found) > 1:
                raise BasicError(AMBIGUOUS)
            if not found:
                self.unresolved.add(base)
                return None
            py = found[0]
        self.names[base] = py
        return py

    def name_for(self, base, spelling):
        """The Python name BASIC's `base` writes: the one it would read (a
        name Python has, then shared), or else the spelling written (a name
        BASIC makes)."""
        py = self.resolve(base, spelling)
        if py is None:
            py = spelling
            self.names[base] = py
            self.created.add(py)
            self.unresolved.discard(base)
        return py

    def array(self, key, base, spelling, n):
        """The list behind array `key` (used with `n` indices): Python's if
        it is newer, else BASIC's, made with indices to 10 if there is none."""
        py = self.names.get(base)
        if py is None:
            py = self.resolve(base, spelling)
        if py is not None:
            v = self.ns.get(py, MISSING)
            if v is not MISSING and v is not self.exports.get(py, MISSING):
                if type(v) is list:
                    return v
                raise BasicError(TYPE)
        a = self.arrays.get(key)
        if a is None:
            a = self.undimmed(key, spelling, n)
        return a

    def dim(self, key, dims, spelling=None):
        if key in self.arrays:
            raise BasicError("REDIM'D ARRAY")
        a = nested(dims, default(key))
        self.arrays[key] = a
        base = base_name(key)
        py = self.names.get(base)
        if py is None:
            py = self.name_for(base, spelling if spelling is not None else base)
        self.ns[py] = a
        self.exports[py] = a
        return a

    def scalar_get(self, key):
        base = base_name(key)
        py = self.names.get(base)
        if py is not None:
            v = self.ns.get(py, MISSING)
            if v is not MISSING and v is not self.exports.get(py, MISSING):
                v = from_python(key[len(base):], v)
                self.vars[key] = v
                return v
        v = self.vars.get(key, MISSING)
        return v if v is not MISSING else self.unset(key)

    def scalar_set(self, key, v):
        self.vars[key] = v
        base = base_name(key)
        py = self.names.get(base)
        if py is None:
            py = self.name_for(base, base)
        self.ns[py] = v
        self.exports[py] = v

    def sys(self, name, args):
        """SYS "name", args: a Python function in the namespace, else an OS
        command or a wrapper (numbers passed to those as BASIC prints them)."""
        f = self.ns.get(name)
        try:
            if f is not None and callable(f):
                f(*args)
            elif self.command is None or not self.command(
                    name, [self.num_text(a) if type(a) is not str else a for a in args]):
                raise BasicError(UNDEF_FN)
        except (BasicError, KeyboardInterrupt):
            raise
        except Exception as e:  # the function's own: its type and message
            self.out.newline_if_needed()
            self.out.write("%s: %s\n" % (type(e).__name__, e))
            raise BasicError("PYTHON")
        finally:
            self.unresolved.clear()  # it may have made names
            self.out.col = 0

    # DATA.

    def _collect(self):
        items = []
        for number in self.order:
            for text, quoted in self.data_items(self.program.lines[number]):
                items.append((text, quoted, number))
        self.data = items

    def next_data(self):
        if self.data is None:
            self._collect()
        if self.data_pos >= len(self.data):
            raise BasicError("OUT OF DATA")
        item = self.data[self.data_pos]
        self.data_pos += 1
        return item

    def restore(self, line):
        if self.data is None:
            self._collect()
        self.data_pos = 0
        if line is not None:
            if line not in self.index:
                raise BasicError(UNDEF_LINE)
            while self.data_pos < len(self.data) and self.data[self.data_pos][2] < line:
                self.data_pos += 1

    def key(self):
        return self.getkey() if self.getkey is not None else ""

    # Immediate commands (statements, so they work in a program too).

    def do_run(self, line):
        self.clear()
        self.cont = None
        if not self.order:
            return self.halt("END")
        if line is None:
            self.li = 0
            self.si = 0
            return True
        return self.goto(line)

    def do_list(self, low, high):
        for number in self.order:
            if low <= number <= high:
                self.out.write("%d %s\n" % (number, self.program.lines[number]))

    def do_new(self):
        self.program.clear()
        self.clear()
        self.code = {}
        self._sync()
        return self.halt("NEW")

    def do_save(self, name):
        """SAVE: the program as LIST shows it, after the dialect's header."""
        path = file_path(name)
        if self.li < 0:
            self.announce("save", name)
        lines = [HEADER + self.DIALECT]
        for number in self.program.numbers():
            lines.append("%d %s" % (number, self.program.lines[number]))
        try:
            with open(path, "w") as f:
                f.write("\n".join(lines) + "\n")
            if os is not None and hasattr(os, "sync"):
                os.sync()  # to the medium now: a failure is SAVE's
        except OSError:
            raise BasicError("WRITE")
        self.file = name
        return None

    def read_file(self, name, run=False):
        """A program file's lines; OtherDialect for one saved in the other
        dialect, carrying `name` and `run` (CHAIN) for KoraCLI."""
        path = file_path(name)
        try:
            with open(path) as f:
                text = f.read()
        except OSError as e:
            raise BasicError("FILE NOT FOUND" if e.args and e.args[0] == 2 else "READ")
        try:
            return read_program(text, self.DIALECT, self.normalize)
        except OtherDialect as e:
            e.name = name
            e.run = run
            raise

    def replace(self, lines, name):
        """The program in memory replaced by `lines` (from file `name`)."""
        self.program.lines = lines
        self.program.version += 1
        self.code = {}
        self.cont = None
        self.file = name
        self._sync()

    def load_typed(self, name):
        """LOAD as if typed, for KoraCLI once it has switched dialect: its
        mistakes reported."""
        self._sync()
        self.li = -1
        try:
            self.do_load(name)
        except BasicError as e:
            self.report(e, None)

    def do_load(self, name):
        """LOAD: the program from the file, in place of the one in memory.
        Typed, it clears the variables, as NEW does; in a program, the new
        one runs from its start with the variables kept (a C64's chaining)."""
        file_path(name)  # a missing name, before anything is said
        direct = self.li < 0
        if direct:
            self.announce("search", name)
        lines = self.read_file(name)
        if direct:
            self.announce("load", name)
        self.replace(lines, name)
        if direct:
            self.clear()
            return None
        self.stack.clear()
        self.data = None
        self.data_pos = 0
        if not self.order:
            return self.halt("END")
        self.li = 0
        self.si = 0
        return True

    def do_renum(self, start, step):
        """RENUM: the lines from `start`, `step` apart, line 0 staying where
        it is, with the line numbers the dialect's statements name. One
        naming a line there is not stays, with a warning."""
        old = [n for n in self.program.numbers() if n != 0]
        if not old:
            return None
        if start < 1 or step < 1 or start + step * (len(old) - 1) > MAX_LINE:
            raise BasicError(QUANTITY)
        new = {0: 0} if 0 in self.program.lines else {}
        for i, n in enumerate(old):
            new[n] = start + step * i
        lines = {}
        for n, text in self.program.lines.items():
            for _, _, target in self.line_refs(text):
                if target not in new:
                    self.out.newline_if_needed()
                    self.warn_ref(target, new[n])
            lines[new[n]] = self.renumber_line(text, new)
        self.program.lines = lines
        self.program.version += 1
        self.code = {}
        self.cont = None
        self.data = None
        self.data_pos = 0
        self._sync()  # the order is the same: a running program's place holds
        return None

    def do_auto(self, start, step):
        """AUTO: KoraCLI types the line numbers, from `start`, `step` apart."""
        if step < 1:
            raise BasicError(QUANTITY)
        self.auto = [start, step]
        return None

    def do_cont(self):
        c = self.cont
        if c is None or c[2] != self.program.version or self.li >= 0:
            raise BasicError("CAN'T CONTINUE")
        self.cont = None
        self.li, self.si = c[0], c[1]
        return True
