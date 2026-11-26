# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's Classic BASIC (project_doc/koracli-spec.md §6, docs/koracli.md):
# Commodore 64 BASIC V2's statements, look and messages (READY., ?SYNTAX
# ERROR IN 10, numbers to 9 digits), with the common GW-BASIC and MSX
# additions (ELSE, WHILE/WEND, ' comments, INKEY$, &H, STRING$, SWAP, MOD).
#
# A line is stored as typed and parsed the first time it runs, into a list of
# Python closures, one per statement, kept until the line changes. A
# statement returns None to go on, or True when it moved the program (a jump
# or an end), having set the interpreter's place (`li`, the line's index in
# the program, -1 for a line typed without a number; `si`, the statement's).
#
# Keywords need no spaces where only a keyword can stand: at a statement's
# start unless the word is assigned to (FORI=1TO10, PRINTI), and after a
# value (1TO10, A ORB). Elsewhere a word is a name, so names may contain or
# start with a keyword (TOTAL, FORMAT); names starting with FN are function
# calls, and REM and DATA at a statement's start begin a remark or data.
#
# The host gives the interpreter `write(text)`, `readline()` (a line typed,
# for INPUT), `getkey()` ("" when no key is waiting, for GET and INKEY$), the
# screen's width, the Python namespace BASIC shares its variables with, and
# `command(name, args)` for SYS (an OS command or wrapper; False if there is
# none).
#
# Shared variables (spec §7): every write of BASIC's also goes into the
# Python namespace, under the name's spelling first used (A, A%, A$ and A()
# are all Python's A), and BASIC remembers the object it put there. A read
# finds Python's value newer when the namespace holds another object: it
# is taken, with the type rules (numbers convert, nothing else does), until
# BASIC writes again. Inside BASIC, A, A%, A$ and A() stay separate. Arrays
# are Python lists, nested for more dimensions, shared as they are.

import math

import basic
from basic import BasicError

try:
    from time import ticks_ms
except ImportError:  # the host tests' MicroPython has no time module

    def ticks_ms():
        return 0

try:
    import gc
except ImportError:
    gc = None

# --- Words -------------------------------------------------------------------------

STATEMENTS = (
    "AUTO", "CLR", "CLS", "CONT", "DATA", "DEF", "DIM", "ELSE", "END", "FOR", "GET",
    "GOSUB", "GOTO", "IF", "INPUT", "LET", "LIST", "LOAD", "NEW", "NEXT", "ON", "PRINT",
    "RANDOMIZE", "READ", "REM", "RENUM", "RESTORE", "RETURN", "RUN", "SAVE", "STOP",
    "SWAP", "SYS", "WEND", "WHILE",
)
OPERATORS = ("AND", "OR", "XOR", "MOD", "NOT")
CLAUSES = ("THEN", "TO", "STEP", "GOTO", "GOSUB", "ELSE")
# Functions: name -> (argument kinds, how many are needed, result kind); "n" a
# number, "s" a string, "*" either.
FUNCTIONS = {
    "ABS": ("n", 1, "n"), "ATN": ("n", 1, "n"), "COS": ("n", 1, "n"), "EXP": ("n", 1, "n"),
    "INT": ("n", 1, "n"), "LOG": ("n", 1, "n"), "SGN": ("n", 1, "n"), "SIN": ("n", 1, "n"),
    "SQR": ("n", 1, "n"), "TAN": ("n", 1, "n"), "RND": ("n", 1, "n"), "FRE": ("*", 1, "n"),
    "POS": ("*", 1, "n"), "ASC": ("s", 1, "n"), "LEN": ("s", 1, "n"), "VAL": ("s", 1, "n"),
    "CHR$": ("n", 1, "s"), "STR$": ("n", 1, "s"), "HEX$": ("n", 1, "s"),
    "SPACE$": ("n", 1, "s"), "LEFT$": ("sn", 2, "s"), "RIGHT$": ("sn", 2, "s"),
    "MID$": ("snn", 2, "s"), "INSTR": ("nss", 2, "n"), "STRING$": ("n*", 2, "s"),
}
KEYWORDS = set(STATEMENTS + OPERATORS + CLAUSES + tuple(FUNCTIONS) +
               ("FN", "INKEY$", "TAB", "SPC"))
# Longest first, for a keyword at a word's start.
BY_LENGTH = sorted(KEYWORDS, key=lambda k: -len(k))

SYNTAX = basic.SYNTAX
AMBIGUOUS = basic.AMBIGUOUS
TYPE = basic.TYPE
QUANTITY = basic.QUANTITY
UNDEF_LINE = basic.UNDEF_LINE
UNDEF_FN = basic.UNDEF_FN

MISSING = basic.MISSING  # no value in the Python namespace


# --- Lexing -------------------------------------------------------------------------
#
# Tokens are (kind, value, start, end): "W" a word (letters, then letters and
# digits, then $ or %: its spelling as typed), "N" a number (its value), "S" a
# string, "O" an operator or punctuation, "R" the raw text after REM or DATA.

def _is_letter(c):
    return "A" <= c <= "Z" or "a" <= c <= "z"


def _is_digit(c):
    return "0" <= c <= "9"


def _number(text, i):
    """A number at `i` (digits, a point, digits, an exponent): (value, end)."""
    n = len(text)
    j = i
    while j < n and _is_digit(text[j]):
        j += 1
    if j < n and text[j] == ".":
        j += 1
        while j < n and _is_digit(text[j]):
            j += 1
    if j < n and text[j] in "eE":
        k = j + 1
        if k < n and text[k] in "+-":
            k += 1
        if k < n and _is_digit(text[k]):
            while k < n and _is_digit(text[k]):
                k += 1
            j = k
    return float(text[i:j]), j


def _hex(text, i):
    """&Hxx at `i`: (value, end), or None."""
    j = i + 2
    while j < len(text) and text[j] in "0123456789abcdefABCDEF":
        j += 1
    if j == i + 2:
        return None
    return float(int(text[i + 2:j], 16)), j


def _word_end(text, i):
    n = len(text)
    j = i + 1
    while j < n and (_is_letter(text[j]) or _is_digit(text[j])):
        j += 1
    if j < n and text[j] in "$%":
        j += 1
    return j


def _starts(text, i, word):
    return text[i:i + len(word)].upper() == word


def _data_end(text, i):
    """Where DATA's items end: a colon outside quotes, or the line's end."""
    quoted = False
    while i < len(text):
        c = text[i]
        if c == '"':
            quoted = not quoted
        elif c == ":" and not quoted:
            return i
        i += 1
    return i


def tokenize(text):
    toks = []
    i = 0
    n = len(text)
    start = True  # a statement starts here: REM and DATA are seen as such
    while i < n:
        c = text[i]
        if c == " " or c == "\t":
            i += 1
            continue
        if c == '"':
            j = text.find('"', i + 1)
            end = n if j < 0 else j + 1
            toks.append(("S", text[i + 1:j if j >= 0 else n], i, end))
            i = end
            start = False
            continue
        if c == "'":
            toks.append(("W", "REM", i, i + 1))
            toks.append(("R", text[i + 1:], i + 1, n))
            break
        if start and _starts(text, i, "REM"):
            toks.append(("W", text[i:i + 3], i, i + 3))
            toks.append(("R", text[i + 3:], i + 3, n))
            break
        if start and _starts(text, i, "DATA"):
            end = _data_end(text, i + 4)
            toks.append(("W", text[i:i + 4], i, i + 4))
            toks.append(("R", text[i + 4:end], i + 4, end))
            i = end
            start = False
            continue
        if _is_digit(c) or (c == "." and i + 1 < n and _is_digit(text[i + 1])):
            value, j = _number(text, i)
            toks.append(("N", value, i, j))
            i = j
            start = False
            continue
        if c == "&" and i + 1 < n and text[i + 1] in "hH":
            h = _hex(text, i)
            if h is not None:
                toks.append(("N", h[0], i, h[1]))
                i = h[1]
                start = False
                continue
        if _is_letter(c):
            j = _word_end(text, i)
            word = text[i:j]
            toks.append(("W", word, i, j))
            i = j
            start = word.upper() in ("THEN", "ELSE")
            continue
        if c == "?":
            toks.append(("W", "PRINT", i, i + 1))
            i += 1
            start = False
            continue
        two = text[i:i + 2]
        if two in ("<=", ">=", "<>", "=<", "=>", "><"):
            toks.append(("O", {"=<": "<=", "=>": ">=", "><": "<>"}.get(two, two), i, i + 2))
            i += 2
            start = False
            continue
        toks.append(("O", c, i, i + 1))
        start = c == ":"
        i += 1
    return toks


def _keyword_prefix(word, allowed):
    """The longest keyword of `allowed` that `word` starts with, or None."""
    up = word.upper()
    for k in BY_LENGTH:
        if k in allowed and up.startswith(k):
            return k
    return None


class _Nothing:
    """A stand-in interpreter, to parse a statement without running it."""

    def __init__(self):
        self.vars = {}
        self.arrays = {}
        self.fns = {}
        self.ns = {}
        self.exports = {}
        self.names = {}

    def scalar(self, key, spelling):
        return (lambda: None), (lambda v: None)


def _assigns(toks, idx):
    """Whether the statement starting at token `idx` is an assignment (as
    the parser decides: TOTAL=1 is, FORI=1TO3 is not)."""
    end = idx
    while end < len(toks) and not (toks[end][0] == "O" and toks[end][1] == ":"):
        end += 1
    c = Compiler(_Nothing(), list(toks[idx:end]))
    try:
        c.assignment()
    except BasicError:
        return False
    return c.peek() is None


AFTER_VALUE = ("AND", "OR", "XOR", "MOD", "TO", "STEP", "THEN", "ELSE", "GOTO", "GOSUB")


def normalize(text):
    """A line as stored: keywords in capitals (? is PRINT), the rest as
    typed."""
    toks = tokenize(text)
    out = []
    at = 0
    start = True
    value_before = False
    for idx, (kind, value, s, e) in enumerate(toks):
        out.append(text[at:s])
        piece = text[s:e]
        if kind == "W":
            up = value.upper()
            nxt = toks[idx + 1] if idx + 1 < len(toks) else None
            assigned = nxt is not None and nxt[0] == "O" and nxt[1] in ("=", "(") and \
                _assigns(toks, idx)
            if piece == "'":
                pass
            elif piece == "?":
                after = text[e:e + 1]
                piece = "PRINT " if _is_letter(after) or _is_digit(after) else "PRINT"
            elif up in KEYWORDS:
                piece = up
            elif up.startswith("FN"):
                piece = "FN" + piece[2:]
            elif start and not assigned:
                k = _keyword_prefix(value, STATEMENTS)
                if k is not None:
                    piece = k + piece[len(k):]
            elif value_before:
                k = _keyword_prefix(value, AFTER_VALUE)
                if k is not None:
                    piece = k + piece[len(k):]
            start = up in ("THEN", "ELSE")
            value_before = up not in KEYWORDS
        else:
            start = kind == "O" and value == ":"
            value_before = kind in ("N", "S") or (kind == "O" and value == ")")
        out.append(piece)
        at = e
    out.append(text[at:])
    return "".join(out)


# --- Numbers and strings -------------------------------------------------------------

def num_str(x):
    """A number as STR$ gives it: a sign (space or -), up to 9 digits, no
    leading zero (.5), E notation below .01 and from 1E9."""
    if x == 0:
        return " 0"
    sign = "-" if x < 0 else " "
    a = -x if x < 0 else x
    s = "%.9g" % a if 0.01 <= a < 1e9 else ""
    if s == "" or "e" in s:
        mant, exp = ("%.8e" % a).split("e")
        if "." in mant:
            mant = mant.rstrip("0").rstrip(".")
        e = int(exp)
        s = "%sE%s%02d" % (mant, "+" if e >= 0 else "-", -e if e < 0 else e)
    elif s.startswith("0."):
        s = s[1:]
    return sign + s


def _number_prefix(s):
    """The number at the start of `s` (spaces before it allowed), and where
    it ends; (None, 0) if there is none."""
    i = 0
    n = len(s)
    while i < n and s[i] == " ":
        i += 1
    if s[i:i + 2] in ("&H", "&h"):
        h = _hex(s, i)
        return (None, 0) if h is None else h
    j = i
    if j < n and s[j] in "+-":
        j += 1
    k = j
    while k < n and (_is_digit(s[k]) or s[k] == "."):
        k += 1
    if s[j:k].count(".") > 1 or not any(_is_digit(c) for c in s[j:k]):
        return None, 0
    value, end = _number(s, j)
    if s[i] == "-":
        value = -value
    return value, end


def val(s):
    value, _ = _number_prefix(s)
    return 0.0 if value is None else value


def parse_number(s):
    """A whole item as a number (INPUT, READ); None if it is not one. An
    empty item is 0."""
    s = s.strip()
    if s == "":
        return 0.0
    value, end = _number_prefix(s)
    if value is None or s[end:].strip():
        return None
    return value


def split_items(raw):
    """DATA's or INPUT's items: (text, quoted) for each, split at commas
    outside quotes."""
    items = []
    i = 0
    n = len(raw)
    while True:
        while i < n and raw[i] == " ":
            i += 1
        if i < n and raw[i] == '"':
            j = raw.find('"', i + 1)
            if j < 0:
                j = n
            items.append((raw[i + 1:j], True))
            i = raw.find(",", j)
            if i < 0:
                return items
        else:
            j = raw.find(",", i)
            end = n if j < 0 else j
            items.append((raw[i:end].strip(), False))
            if j < 0:
                return items
            i = j
        i += 1


to_int = basic.to_int
finite = basic.finite


def _index(v):
    i = int(v)
    if i < 0:
        raise BasicError(QUANTITY)
    return i


def key_char(ch, key):
    """A key as GET and INKEY$ give it: its character, or the C64's codes
    for Return, the cursor keys, Home and Delete."""
    import keys
    codes = {keys.ENTER: 13, keys.KP_ENTER: 13, keys.BACKSPACE: 20, keys.DELETE: 20,
             keys.DOWN: 17, keys.UP: 145, keys.RIGHT: 29, keys.LEFT: 157, keys.HOME: 19}
    if key in codes:
        return chr(codes[key])
    return ch or ""


_nested = basic.nested
_base = basic.base_name
_from_python = basic.from_python
_default = basic.default
_converter = basic.converter


# --- Parsing into closures ------------------------------------------------------------

class Compiler:
    """One line's tokens into statements: closures and their keywords."""

    def __init__(self, rt, toks):
        self.rt = rt
        self.toks = toks
        self.pos = 0

    # Tokens.

    def peek(self):
        return self.toks[self.pos] if self.pos < len(self.toks) else None

    def next(self):
        t = self.peek()
        if t is None:
            raise BasicError(SYNTAX)
        self.pos += 1
        return t

    def is_op(self, value):
        t = self.peek()
        return t is not None and t[0] == "O" and t[1] == value

    def expect_op(self, value):
        if not self.is_op(value):
            raise BasicError(SYNTAX)
        self.pos += 1

    def word_kw(self, allowed):
        """The keyword of `allowed` the next word is or starts with (split
        off from the rest of the word), or None."""
        t = self.peek()
        if t is None or t[0] != "W":
            return None
        up = t[1].upper()
        if up in allowed:
            return up
        k = _keyword_prefix(t[1], allowed)
        if k is None:
            return None
        self.split(k)
        return k

    def split(self, k):
        """The next word as keyword `k` and what follows it, lexed again."""
        kind, value, s, e = self.toks[self.pos]
        rest = tokenize(" " * (s + len(k)) + value[len(k):])
        self.toks[self.pos:self.pos + 1] = [("W", k, s, s + len(k))] + rest

    def take_kw(self, allowed):
        k = self.word_kw(allowed)
        if k is not None:
            self.pos += 1
        return k

    def expect_kw(self, k):
        if self.take_kw((k,)) is None:
            raise BasicError(SYNTAX)

    def at_end(self, in_then=False):
        t = self.peek()
        if t is None or (t[0] == "O" and t[1] == ":"):
            return True
        return in_then and t[0] == "W" and t[1].upper() == "ELSE"

    def line_number(self):
        t = self.next()
        if t[0] != "N" or t[1] != int(t[1]):
            raise BasicError(SYNTAX)
        return int(t[1])

    # Expressions: (closure, kind), kind "n" or "s".

    def num(self):
        f, k = self.expr()
        if k != "n":
            raise BasicError(TYPE)
        return f

    def string(self):
        f, k = self.expr()
        if k != "s":
            raise BasicError(TYPE)
        return f

    def expr(self):
        f, k = self.and_()
        while True:
            op = self.word_kw(("OR", "XOR"))
            if op is None:
                return f, k
            self.pos += 1
            g, k2 = self.and_()
            if k != "n" or k2 != "n":
                raise BasicError(TYPE)
            if op == "OR":
                f = (lambda a, b: lambda: float(to_int(a()) | to_int(b())))(f, g)
            else:
                f = (lambda a, b: lambda: float(to_int(a()) ^ to_int(b())))(f, g)

    def and_(self):
        f, k = self.not_()
        while self.word_kw(("AND",)):
            self.pos += 1
            g, k2 = self.not_()
            if k != "n" or k2 != "n":
                raise BasicError(TYPE)
            f = (lambda a, b: lambda: float(to_int(a()) & to_int(b())))(f, g)
        return f, k

    def not_(self):
        t = self.peek()
        if t is not None and t[0] == "W" and t[1].upper() == "NOT":
            self.pos += 1
            f, k = self.not_()
            if k != "n":
                raise BasicError(TYPE)
            return (lambda a: lambda: float(~to_int(a())))(f), "n"
        return self.relation()

    def relation(self):
        f, k = self.add()
        while True:
            t = self.peek()
            if t is None or t[0] != "O" or t[1] not in ("=", "<", ">", "<=", ">=", "<>"):
                return f, k
            self.pos += 1
            g, k2 = self.add()
            if k != k2:
                raise BasicError(TYPE)
            f, k = _compare(t[1], f, g), "n"

    def add(self):
        f, k = self.mod()
        while True:
            if self.is_op("+"):
                self.pos += 1
                g, k2 = self.mod()
                if k != k2:
                    raise BasicError(TYPE)
                f = (lambda a, b: lambda: a() + b())(f, g)
            elif self.is_op("-"):
                self.pos += 1
                g, k2 = self.mod()
                if k != "n" or k2 != "n":
                    raise BasicError(TYPE)
                f = (lambda a, b: lambda: a() - b())(f, g)
            else:
                return f, k

    def mod(self):
        f, k = self.idiv()
        while self.word_kw(("MOD",)):
            self.pos += 1
            g, k2 = self.idiv()
            if k != "n" or k2 != "n":
                raise BasicError(TYPE)
            f = (lambda a, b: lambda: float(math.fmod(_round(a()), _round(b(), True))))(f, g)
        return f, k

    def idiv(self):
        f, k = self.mul()
        while self.is_op("\\"):
            self.pos += 1
            g, k2 = self.mul()
            if k != "n" or k2 != "n":
                raise BasicError(TYPE)
            f = (lambda a, b: lambda: float(int(_round(a()) / _round(b(), True))))(f, g)
        return f, k

    def mul(self):
        f, k = self.unary()
        while True:
            if self.is_op("*"):
                op = "*"
            elif self.is_op("/"):
                op = "/"
            else:
                return f, k
            self.pos += 1
            g, k2 = self.unary()
            if k != "n" or k2 != "n":
                raise BasicError(TYPE)
            if op == "*":
                f = (lambda a, b: lambda: a() * b())(f, g)
            else:
                f = (lambda a, b: lambda: a() / b())(f, g)

    def unary(self):
        if self.is_op("-"):
            self.pos += 1
            f, k = self.unary()
            if k != "n":
                raise BasicError(TYPE)
            return (lambda a: lambda: -a())(f), "n"
        if self.is_op("+"):
            self.pos += 1
            f, k = self.unary()
            if k != "n":
                raise BasicError(TYPE)
            return f, "n"
        return self.power()

    def power(self):
        f, k = self.atom()
        while self.is_op("^"):
            self.pos += 1
            g = self.exponent()
            if k != "n":
                raise BasicError(TYPE)
            f = (lambda a, b: lambda: _power(a(), b()))(f, g)
        return f, k

    def exponent(self):
        """What ^ raises to: an atom, signs allowed (2^-1)."""
        if self.is_op("-"):
            self.pos += 1
            g = self.exponent()
            return lambda: -g()
        if self.is_op("+"):
            self.pos += 1
            return self.exponent()
        f, k = self.atom()
        if k != "n":
            raise BasicError(TYPE)
        return f

    def atom(self):
        t = self.next()
        kind, value = t[0], t[1]
        if kind == "N":
            return (lambda v: lambda: v)(value), "n"
        if kind == "S":
            return (lambda v: lambda: v)(value), "s"
        if kind == "O" and value == "(":
            f, k = self.expr()
            self.expect_op(")")
            return f, k
        if kind != "W":
            raise BasicError(SYNTAX)
        up = value.upper()
        if up.startswith("FN"):
            return self.fn_call(value)
        if up == "INKEY$":
            rt = self.rt
            return (lambda: rt.key()), "s"
        if up in FUNCTIONS and self.is_op("("):
            return self.function(up)
        if up in ("TI", "TI$"):
            rt = self.rt
            if up == "TI":
                return (lambda: float(rt.jiffies())), "n"
            return (lambda: rt.ti_str()), "s"
        if up in KEYWORDS:
            raise BasicError(SYNTAX)
        self.pos -= 1
        get, _, kind = self.variable()
        return get, kind

    def args(self):
        """A parenthesised argument list: [(closure, kind)]."""
        self.expect_op("(")
        out = [self.expr()]
        while self.is_op(","):
            self.pos += 1
            out.append(self.expr())
        self.expect_op(")")
        return out

    def function(self, name):
        kinds, needed, result = FUNCTIONS[name]
        a = self.args()
        if name == "INSTR" and len(a) == 2:
            a = [((lambda: 1.0), "n")] + a  # INSTR(s, t) starts at 1
        if not needed <= len(a) <= len(kinds):
            raise BasicError(SYNTAX)
        for (f, k), want in zip(a, kinds):
            if want != "*" and k != want:
                raise BasicError(TYPE)
        return _builtin(name, [f for f, _ in a], a, self.rt), result

    def fn_call(self, word):
        name = word[2:]
        if name == "":
            t = self.next()
            if t[0] != "W":
                raise BasicError(SYNTAX)
            name = t[1]
        key = name.upper()
        kind = "s" if key.endswith("$") else "n"
        a = self.args()
        fns = self.rt.fns

        def call():
            d = fns.get(key)
            if d is None:
                raise BasicError(UNDEF_FN)
            return d(a)
        return call, kind

    def variable(self):
        """A variable or array element: (getter, setter, kind). The setter
        takes a value of the right kind and converts it to the variable's."""
        t = self.next()
        if t[0] != "W":
            raise BasicError(SYNTAX)
        name = t[1]
        key = name.upper()
        if key in KEYWORDS or key.startswith("FN") or key in ("TI", "TI$"):
            raise BasicError(SYNTAX)
        kind = "s" if key.endswith("$") else "n"
        conv = _converter(key)
        base = _base(key)
        spelling = name[:len(base)]
        suffix = key[len(base):]
        rt = self.rt
        if self.is_op("("):
            self.pos += 1
            idx = [self.num()]
            while self.is_op(","):
                self.pos += 1
                idx.append(self.num())
            self.expect_op(")")
            return _element(rt, key, base, spelling, suffix, idx, conv) + (kind,)
        get, set_ = rt.scalar(key, spelling)
        return get, set_, kind

    # Statements.

    def statements(self):
        out, kinds = [], []
        self.stmt_list(out, kinds, False)
        if self.peek() is not None:
            raise BasicError(SYNTAX)
        return out, kinds

    def at_else(self):
        t = self.peek()
        return t is not None and t[0] == "W" and t[1].upper() == "ELSE"

    def stmt_list(self, out, kinds, in_then):
        """Statements to the line's end, or (`in_then`) to an ELSE."""
        while True:
            if self.peek() is None or (in_then and self.at_else()):
                return
            if self.is_op(":"):
                self.pos += 1
                continue
            self.statement(out, kinds, in_then)
            if self.peek() is None or (in_then and self.at_else()):
                return
            self.expect_op(":")

    def statement(self, out, kinds, in_then):
        t = self.peek()
        if t[0] != "W":
            raise BasicError(SYNTAX)
        up = t[1].upper()
        if up not in STATEMENTS:
            # An assignment, or a keyword without its space (PRINTI, FORI=1):
            # the assignment is tried first, so names may start with one.
            error = BasicError(SYNTAX)
            if up == "TI$" or (up not in KEYWORDS and not up.startswith("FN")):
                pos, toks = self.pos, list(self.toks)
                try:
                    f = self.assignment()
                    if self.at_end(in_then):
                        out.append(f)
                        kinds.append(None)
                        return
                except BasicError as e:
                    error = e
                self.pos, self.toks = pos, toks
            k = _keyword_prefix(t[1], STATEMENTS)
            if k is None:
                raise error
            self.split(k)
            up = k
        self.pos += 1
        before = len(out)
        getattr(self, "s_" + up)(out, kinds, in_then)
        if len(kinds) == before and len(out) > before:
            kinds.append(up)
        while len(kinds) < len(out):
            kinds.append(None)

    def assignment(self):
        t = self.peek()
        if t is not None and t[0] == "W" and t[1].upper() == "TI$":
            self.pos += 1
            self.expect_op("=")
            f = self.string()
            rt = self.rt
            return lambda: rt.set_ti(f())
        get, set_, kind = self.variable()
        self.expect_op("=")
        f, k = self.expr()
        if k != kind:
            raise BasicError(TYPE)

        def let():
            set_(f())
        return let

    def s_LET(self, out, kinds, in_then):
        out.append(self.assignment())

    def s_REM(self, out, kinds, in_then):
        if self.peek() is not None and self.peek()[0] == "R":
            self.pos += 1
        self.pos = len(self.toks)  # a remark ends the line, ELSE and all
        out.append(lambda: None)

    def s_DATA(self, out, kinds, in_then):
        if self.peek() is not None and self.peek()[0] == "R":
            self.pos += 1
        out.append(lambda: None)

    def s_PRINT(self, out, kinds, in_then):
        parts = []
        newline = True
        while not self.at_end(in_then):
            if self.is_op(";"):
                self.pos += 1
                newline = False
                continue
            if self.is_op(","):
                self.pos += 1
                parts.append((",", None))
                newline = False
                continue
            t = self.peek()
            if t[0] == "W" and t[1].upper() in ("TAB", "SPC") and \
                    self.pos + 1 < len(self.toks) and self.toks[self.pos + 1][1] == "(":
                self.pos += 2
                f = self.num()
                self.expect_op(")")
                parts.append((t[1].upper(), f))
                newline = False
                continue
            f, k = self.expr()
            parts.append((k, f))
            newline = True
        out.append(_print(self.rt, parts, newline))

    def s_GOTO(self, out, kinds, in_then):
        out.append(_goto(self.rt, self.line_number()))

    def s_GOSUB(self, out, kinds, in_then):
        out.append(_gosub(self.rt, self.line_number()))

    def s_RETURN(self, out, kinds, in_then):
        rt = self.rt
        out.append(rt.do_return)

    def s_END(self, out, kinds, in_then):
        rt = self.rt
        out.append(lambda: rt.halt("END"))

    def s_STOP(self, out, kinds, in_then):
        rt = self.rt
        out.append(lambda: rt.halt("STOP"))

    def s_IF(self, out, kinds, in_then):
        cond = self.num()
        skip = [0]
        out.append(_if(self.rt, cond, skip))
        kinds.append("IF")
        if self.take_kw(("THEN",)) is not None:
            t = self.peek()
            if t is not None and t[0] == "N":
                out.append(_goto(self.rt, self.line_number()))
            else:
                self.stmt_list(out, kinds, True)
        elif self.take_kw(("GOTO",)) is not None:
            out.append(_goto(self.rt, self.line_number()))
        else:
            raise BasicError(SYNTAX)
        while len(kinds) < len(out):
            kinds.append(None)
        if self.take_kw(("ELSE",)) is not None:
            end = [0]
            out.append(_skip(self.rt, end))
            kinds.append(None)
            skip[0] = len(out)
            t = self.peek()
            if t is not None and t[0] == "N":
                out.append(_goto(self.rt, self.line_number()))
            else:
                self.stmt_list(out, kinds, True)
            while len(kinds) < len(out):
                kinds.append(None)
            end[0] = len(out)
        else:
            skip[0] = -1  # the rest of the line

    def s_ELSE(self, out, kinds, in_then):
        # An ELSE no IF took: the rest of the line is skipped.
        self.pos = len(self.toks)
        end = [-1]
        out.append(_skip(self.rt, end))

    def s_FOR(self, out, kinds, in_then):
        t = self.peek()
        if t is None or t[0] != "W" or (self.pos + 1 < len(self.toks) and
                                         self.toks[self.pos + 1][1] == "("):
            raise BasicError(SYNTAX)  # a variable, not an array's element
        key = t[1].upper()
        get, set_, kind = self.variable()
        if kind != "n":
            raise BasicError(TYPE)
        self.expect_op("=")
        start = self.num()
        self.expect_kw("TO")
        limit = self.num()
        step = None
        if self.take_kw(("STEP",)) is not None:
            step = self.num()
        rt = self.rt

        def for_():
            set_(start())
            rt.push_for(key, limit(), step() if step is not None else 1.0)
        out.append(for_)

    def s_NEXT(self, out, kinds, in_then):
        names = []
        if not self.at_end(in_then):
            names.append(self.next_name())
            while self.is_op(","):
                self.pos += 1
                names.append(self.next_name())
        rt = self.rt
        if not names:
            out.append(lambda: rt.do_next(None))
            return
        if len(names) == 1:
            k = names[0]
            out.append(lambda: rt.do_next(k))
            return

        def next_all():
            for k in names:
                if rt.do_next(k):
                    return True
            return None
        out.append(next_all)

    def next_name(self):
        t = self.next()
        if t[0] != "W" or t[1].upper() in KEYWORDS:
            raise BasicError(SYNTAX)
        return t[1].upper()

    def s_WHILE(self, out, kinds, in_then):
        cond = self.num()
        rt = self.rt
        out.append(lambda: rt.do_while(cond))

    def s_WEND(self, out, kinds, in_then):
        rt = self.rt
        out.append(rt.do_wend)

    def s_ON(self, out, kinds, in_then):
        sel = self.num()
        how = self.take_kw(("GOTO", "GOSUB"))
        if how is None:
            raise BasicError(SYNTAX)
        targets = [self.line_number()]
        while self.is_op(","):
            self.pos += 1
            targets.append(self.line_number())
        rt = self.rt

        def on():
            n = to_int(sel())
            if n < 0:
                raise BasicError(QUANTITY)
            if n == 0 or n > len(targets):
                return None
            if how == "GOSUB":
                rt.push_gosub()
            return rt.goto(targets[n - 1])
        out.append(on)

    def s_DIM(self, out, kinds, in_then):
        decls = []
        while True:
            t = self.next()
            if t[0] != "W" or t[1].upper() in KEYWORDS:
                raise BasicError(SYNTAX)
            key = t[1].upper()
            self.expect_op("(")
            dims = [self.num()]
            while self.is_op(","):
                self.pos += 1
                dims.append(self.num())
            self.expect_op(")")
            decls.append((key, dims, t[1][:len(_base(key))]))
            if not self.is_op(","):
                break
            self.pos += 1
        rt = self.rt

        def dim():
            for key, dims, spelling in decls:
                rt.dim(key, [_index(d()) for d in dims], spelling)
        out.append(dim)

    def s_DEF(self, out, kinds, in_then):
        t = self.next()
        if t[0] != "W" or not t[1].upper().startswith("FN"):
            raise BasicError(SYNTAX)
        name = t[1][2:]
        if name == "":
            t = self.next()
            if t[0] != "W":
                raise BasicError(SYNTAX)
            name = t[1]
        key = name.upper()
        params = []
        self.expect_op("(")
        while True:
            p = self.next()
            if p[0] != "W" or p[1].upper() in KEYWORDS:
                raise BasicError(SYNTAX)
            params.append((p[1].upper(), p[1][:len(_base(p[1]))]))
            if not self.is_op(","):
                break
            self.pos += 1
        self.expect_op(")")
        self.expect_op("=")
        body, kind = self.expr()
        if kind != ("s" if key.endswith("$") else "n"):
            raise BasicError(TYPE)
        rt = self.rt
        out.append(lambda: rt.define(key, params, body))

    def s_READ(self, out, kinds, in_then):
        targets = [self.variable()]
        while self.is_op(","):
            self.pos += 1
            targets.append(self.variable())
        rt = self.rt

        def read():
            for _, set_, kind in targets:
                text, quoted, line = rt.next_data()
                if kind == "s":
                    set_(text)
                else:
                    v = None if quoted else parse_number(text)
                    if v is None:
                        raise BasicError(SYNTAX, line)
                    set_(v)
        out.append(read)

    def s_RESTORE(self, out, kinds, in_then):
        line = None
        if not self.at_end(in_then):
            line = self.line_number()
        rt = self.rt
        out.append(lambda: rt.restore(line))

    def s_INPUT(self, out, kinds, in_then):
        prompt = "? "
        t = self.peek()
        if t is not None and t[0] == "S":
            self.pos += 1
            if self.is_op(";"):
                prompt = t[1] + "? "
            elif self.is_op(","):
                prompt = t[1]
            else:
                raise BasicError(SYNTAX)
            self.pos += 1
        targets = [self.variable()]
        while self.is_op(","):
            self.pos += 1
            targets.append(self.variable())
        rt = self.rt
        out.append(lambda: rt.do_input(prompt, targets))

    def s_GET(self, out, kinds, in_then):
        targets = [self.variable()]
        while self.is_op(","):
            self.pos += 1
            targets.append(self.variable())
        rt = self.rt

        def get():
            if rt.li < 0:
                raise BasicError("ILLEGAL DIRECT")
            for _, set_, kind in targets:
                c = rt.key()
                if kind == "s":
                    set_(c)
                elif c == "":
                    set_(0.0)
                elif _is_digit(c):
                    set_(float(c))
                else:
                    raise BasicError(SYNTAX)
        out.append(get)

    def s_SWAP(self, out, kinds, in_then):
        get1, set1, k1 = self.variable()
        self.expect_op(",")
        get2, set2, k2 = self.variable()
        if k1 != k2:
            raise BasicError(TYPE)

        def swap():
            a, b = get1(), get2()
            set1(b)
            set2(a)
        out.append(swap)

    def s_SYS(self, out, kinds, in_then):
        name, kind = self.expr()
        if kind != "s":
            raise BasicError(SYNTAX)  # SYS with an address comes in phase 4
        args = []
        while self.is_op(","):
            self.pos += 1
            args.append(self.expr()[0])
        rt = self.rt
        out.append(lambda: rt.sys(name(), [f() for f in args]))

    def s_CLR(self, out, kinds, in_then):
        rt = self.rt
        out.append(rt.clear)

    def s_CLS(self, out, kinds, in_then):
        rt = self.rt

        def cls():
            rt.out.write("\x1b[2J\x1b[H")
            rt.out.col = 0
        out.append(cls)

    def s_RANDOMIZE(self, out, kinds, in_then):
        seed = None if self.at_end(in_then) else self.num()
        rt = self.rt
        out.append(lambda: rt.random.seed(seed() * 1000003 if seed is not None else ticks_ms()))

    def s_RUN(self, out, kinds, in_then):
        line = None if self.at_end(in_then) else self.line_number()
        rt = self.rt
        out.append(lambda: rt.do_run(line))

    def s_LIST(self, out, kinds, in_then):
        low, high = 0, basic.MAX_LINE
        if not self.at_end(in_then):
            if not self.is_op("-"):
                low = high = self.line_number()
            if self.is_op("-"):
                self.pos += 1
                high = basic.MAX_LINE if self.at_end(in_then) else self.line_number()
        rt = self.rt
        out.append(lambda: rt.do_list(low, high))

    def file_name(self, in_then):
        """SAVE's and LOAD's name, a string; a C64's device and secondary
        address after it (`,8`, `,8,1`) are read and left alone."""
        if self.at_end(in_then):
            raise BasicError("MISSING FILE NAME")
        name, kind = self.expr()
        if kind != "s":
            raise BasicError(TYPE)
        while self.is_op(","):
            self.pos += 1
            self.num()
        return name

    def numbering(self, in_then):
        """RENUM's and AUTO's [start[, step]], 10 and 10 when not given."""
        start = step = 10
        if not self.at_end(in_then):
            if not self.is_op(","):
                start = self.line_number()
            if self.is_op(","):
                self.pos += 1
                step = self.line_number()
        return start, step

    def s_SAVE(self, out, kinds, in_then):
        name = self.file_name(in_then)
        rt = self.rt
        out.append(lambda: rt.do_save(name()))

    def s_LOAD(self, out, kinds, in_then):
        name = self.file_name(in_then)
        rt = self.rt
        out.append(lambda: rt.do_load(name()))

    def s_RENUM(self, out, kinds, in_then):
        start, step = self.numbering(in_then)
        rt = self.rt
        out.append(lambda: rt.do_renum(start, step))

    def s_AUTO(self, out, kinds, in_then):
        start, step = self.numbering(in_then)
        rt = self.rt
        out.append(lambda: rt.do_auto(start, step))

    def s_NEW(self, out, kinds, in_then):
        rt = self.rt
        out.append(rt.do_new)

    def s_CONT(self, out, kinds, in_then):
        rt = self.rt
        out.append(rt.do_cont)


# --- Files and line numbers ----------------------------------------------------------

# The words before a line number (or ON's list of them).
REFERENCES = ("GOTO", "GOSUB", "THEN", "ELSE", "RESTORE", "RUN")


def line_refs(text):
    """The line numbers a program line names: (start, end, number) for each
    after GOTO, GOSUB (and ON's lists), THEN, ELSE, RESTORE and RUN. GOTO,
    GOSUB, THEN and ELSE may be joined to the number (GOTO30), as the
    parser takes them."""
    toks = tokenize(text)
    refs = []
    i = 0
    while i < len(toks):
        kind, value, s, e = toks[i]
        i += 1
        if kind != "W":
            continue
        up = value.upper()
        if up not in REFERENCES:
            k = _keyword_prefix(value, CLAUSES)
            if k is None or k == "TO" or k == "STEP":
                continue
            if i < len(toks) and toks[i][0] == "O" and toks[i][1] == "=":
                continue  # a name assigned to (GOTO1 = 5)
            # The rest of the word, lexed again where it stands.
            toks[i:i] = tokenize(" " * (s + len(k)) + value[len(k):])
            up = k
        while i < len(toks) and toks[i][0] == "N" and toks[i][1] == int(toks[i][1]):
            refs.append((toks[i][2], toks[i][3], int(toks[i][1])))
            i += 1
            if up not in ("GOTO", "GOSUB") or i + 1 >= len(toks) or \
                    toks[i][0] != "O" or toks[i][1] != ",":
                break
            i += 1  # ON's list goes on
    return refs


def renumber_line(text, new):
    """`text` with the line numbers it names changed as `new` maps them."""
    for s, e, n in reversed(line_refs(text)):
        if n in new:
            text = text[:s] + str(new[n]) + text[e:]
    return text


def _round(v, divisor=False):
    """An operand of MOD and \\, rounded to an integer; a zero divisor is
    division by zero."""
    r = math.floor(v + 0.5)
    if divisor and r == 0:
        raise ZeroDivisionError
    return r


def _power(a, b):
    try:
        return finite(math.pow(a, b))
    except ValueError:
        raise BasicError(QUANTITY)


def _compare(op, a, b):
    if op == "=":
        return lambda: -1.0 if a() == b() else 0.0
    if op == "<>":
        return lambda: -1.0 if a() != b() else 0.0
    if op == "<":
        return lambda: -1.0 if a() < b() else 0.0
    if op == ">":
        return lambda: -1.0 if a() > b() else 0.0
    if op == "<=":
        return lambda: -1.0 if a() <= b() else 0.0
    return lambda: -1.0 if a() >= b() else 0.0


def _element(rt, key, base, spelling, suffix, idx, conv):
    last = idx[-1]
    outer = idx[:-1]

    def where():
        """The list holding the element, and its index there."""
        lst = rt.array(key, base, spelling, len(idx))
        for f in outer:
            i = _index(f())
            if i >= len(lst) or type(lst[i]) is not list:
                raise BasicError("BAD SUBSCRIPT")
            lst = lst[i]
        i = _index(last())
        if i >= len(lst):
            raise BasicError("BAD SUBSCRIPT")
        return lst, i

    def get():
        lst, i = where()
        v = lst[i]
        if type(v) is float and suffix == "":
            return v
        if type(v) is list:
            raise BasicError("BAD SUBSCRIPT")  # fewer indices than dimensions
        return _from_python(suffix, v)

    def set_(v):
        lst, i = where()
        lst[i] = v if conv is None else conv(v)
    return get, set_


def _builtin(name, fs, a, rt):
    f = fs[0]
    if name == "ABS":
        return lambda: abs(f())
    if name == "ATN":
        return lambda: math.atan(f())
    if name == "COS":
        return lambda: math.cos(f())
    if name == "EXP":
        return lambda: finite(math.exp(f()))
    if name == "INT":
        return lambda: float(math.floor(f()))
    if name == "LOG":
        def log():
            x = f()
            if x <= 0:
                raise BasicError(QUANTITY)
            return math.log(x)
        return log
    if name == "SGN":
        def sgn():
            x = f()
            return 1.0 if x > 0 else -1.0 if x < 0 else 0.0
        return sgn
    if name == "SIN":
        return lambda: math.sin(f())
    if name == "SQR":
        def sqr():
            x = f()
            if x < 0:
                raise BasicError(QUANTITY)
            return math.sqrt(x)
        return sqr
    if name == "TAN":
        return lambda: math.tan(f())
    if name == "RND":
        return lambda: rt.rnd(f())
    if name == "FRE":
        return lambda: float(gc.mem_free()) if gc is not None else 0.0
    if name == "POS":
        return lambda: float(rt.out.col)
    if name == "ASC":
        def asc():
            s = f()
            if s == "":
                raise BasicError(QUANTITY)
            return float(ord(s[0]))
        return asc
    if name == "LEN":
        return lambda: float(len(f()))
    if name == "VAL":
        return lambda: val(f())
    if name == "CHR$":
        def chr_():
            n = to_int(f())
            if n < 0 or n > 0x10FFFF:
                raise BasicError(QUANTITY)
            return chr(n)
        return chr_
    if name == "STR$":
        return lambda: num_str(f())
    if name == "HEX$":
        return lambda: "%X" % (to_int(f()) & 0xFFFFFFFF)
    if name == "SPACE$":
        return lambda: " " * _count(f())
    if name == "LEFT$":
        g = fs[1]
        return lambda: f()[:_count(g())]
    if name == "RIGHT$":
        g = fs[1]

        def right():
            s = f()
            n = _count(g())
            return s[len(s) - n:] if n < len(s) else s
        return right
    if name == "MID$":
        g = fs[1]
        h = fs[2] if len(fs) > 2 else None

        def mid():
            s = f()
            start = to_int(g())
            if start < 1:
                raise BasicError(QUANTITY)
            if h is None:
                return s[start - 1:]
            return s[start - 1:start - 1 + _count(h())]
        return mid
    if name == "INSTR":
        s, t = fs[1], fs[2]

        def instr():
            start = to_int(f())
            if start < 1:
                raise BasicError(QUANTITY)
            return float(s().find(t(), start - 1) + 1)
        return instr
    if name == "STRING$":
        g, kind = a[1]

        def string_():
            n = _count(f())
            c = g()
            if kind == "n":
                c = chr(to_int(c))
            elif c == "":
                raise BasicError(QUANTITY)
            return c[0] * n
        return string_
    raise BasicError(SYNTAX)


def _count(v):
    n = to_int(v)
    if n < 0:
        raise BasicError(QUANTITY)
    return n


def _print(rt, parts, newline):
    out = rt.out
    zone = 10

    def print_():
        col = out.col
        width = out.width
        buf = []
        try:
            items(buf, col, width)
        except BaseException:
            out.write("".join(buf))  # what came before the error, as a C64 shows it
            raise
        if newline:
            buf.append("\n")
        out.write("".join(buf))

    def items(buf, col, width):
        for kind, f in parts:
            if kind == "n":
                s = num_str(finite(f())) + " "
            elif kind == "s":
                s = f()
            elif kind == ",":
                s = " " * (zone - col % zone)
            elif kind == "TAB":
                n = _count(f()) % width
                s = " " * (n - col) if n > col else ""
            else:  # SPC
                s = " " * _count(f())
            if s:
                buf.append(s)
                nl = s.rfind("\n")
                col = (len(s) - nl - 1 if nl >= 0 else col + len(s)) % width
    return print_


def _goto(rt, line):
    return lambda: rt.goto(line)


def _gosub(rt, line):
    def gosub():
        rt.push_gosub()
        return rt.goto(line)
    return gosub


def _if(rt, cond, skip):
    def if_():
        if cond() != 0:
            return None
        rt.si = skip[0] if skip[0] >= 0 else 1 << 30
        return True
    return if_


def _skip(rt, end):
    def skip():
        rt.si = end[0] if end[0] >= 0 else 1 << 30
        return True
    return skip


# --- The interpreter ----------------------------------------------------------------

class Interpreter(basic.Runtime):
    """Classic BASIC: the program, its variables and its run, over the
    runtime both dialects share (basic.Runtime)."""

    DIALECT = "Classic"

    def __init__(self, write, readline=None, getkey=None, width=80, ns=None, command=None):
        super().__init__(write, readline, getkey, width, ns, command, normalize, ticks_ms())
        self.fns = {}
        self.ti_base = 0

    # What the runtime asks of the dialect.

    def compile_line(self, text):
        return Compiler(self, tokenize(text)).statements()

    def data_items(self, text):
        items = []
        toks = tokenize(text)
        for j, t in enumerate(toks):
            if t[0] == "W" and t[1].upper() == "DATA" and j + 1 < len(toks) and \
                    toks[j + 1][0] == "R":
                items += split_items(toks[j + 1][1])
        return items

    def report(self, e, line):
        if e.line is not None:
            line = e.line
        self.out.newline_if_needed()
        self.out.write("?%s ERROR%s\n" % (e.name, " IN %d" % line if line is not None else ""))

    def report_break(self, line, how):
        self.out.write("BREAK IN %d\n" % line if line is not None else "BREAK\n")

    def announce(self, what, name):
        if what == "save":
            self.out.write("SAVING %s\n" % name)
        elif what == "search":
            self.out.write("SEARCHING FOR %s\n" % name)
        else:
            self.out.write("LOADING\n")

    def line_refs(self, text):
        return line_refs(text)

    def renumber_line(self, text, new):
        return renumber_line(text, new)

    def warn_ref(self, target, line):
        self.out.write("?UNDEF'D STATEMENT %d IN %d\n" % (target, line))

    def num_text(self, x):
        return num_str(x).strip()

    # Variables: DEF FN's functions too.

    def clear(self):
        super().clear()
        self.fns.clear()

    def define(self, key, params, body):
        variables = self.vars
        ns = self.ns
        exports = self.exports
        keys = [p for p, _ in params]
        convs = [_converter(p) for p in keys]
        kinds = ["s" if p.endswith("$") else "n" for p in keys]

        def call(args):
            if len(args) != len(keys):
                raise BasicError(SYNTAX)
            values = []
            for (f, k), want, conv in zip(args, kinds, convs):
                if k != want:
                    raise BasicError(TYPE)
                v = f()
                values.append(v if conv is None else conv(v))
            # The parameters are the function's own: BASIC's variables of
            # those names wait, and Python's are not read meanwhile (their
            # value counts as BASIC's while the body runs).
            saved = [variables.get(p, MISSING) for p in keys]
            shadowed = []
            for p, spelling in params:
                base = _base(p)
                py = self.names.get(base)
                if py is None:
                    py = self.resolve(base, spelling)
                if py is not None:
                    shadowed.append((py, exports.get(py, MISSING)))
                    exports[py] = ns.get(py, MISSING)
            for p, v in zip(keys, values):
                variables[p] = v
            try:
                return body()
            finally:
                for p, v in zip(keys, saved):
                    if v is MISSING:
                        variables.pop(p, None)
                    else:
                        variables[p] = v
                for py, v in reversed(shadowed):
                    if v is MISSING:
                        exports.pop(py, None)
                    else:
                        exports[py] = v
        self.fns[key] = call

    # Input.

    def do_input(self, prompt, targets):
        if self.li < 0:
            raise BasicError("ILLEGAL DIRECT")
        while True:
            self.out.write(prompt)
            items = self._read_items()
            if items is None:
                return None  # Return on its own: the variables stay as they were
            while len(items) < len(targets):
                self.out.write("?? ")
                more = self._read_items()
                items += more if more is not None else [("", False)]
            values = []
            for (text, quoted), (_, _, kind) in zip(items, targets):
                if kind == "s":
                    values.append(text)
                    continue
                v = None if quoted else parse_number(text)
                if v is None:
                    break
                values.append(v)
            if len(values) < len(targets):
                self.out.write("?REDO FROM START\n")
                continue
            if len(items) > len(targets):
                self.out.write("?EXTRA IGNORED\n")
            for (_, set_, _), v in zip(targets, values):
                set_(v)
            return None

    def _read_items(self):
        if self.readline is None:
            raise BasicError("ILLEGAL DIRECT")
        line = self.readline()
        self.out.col = 0
        if line == "":
            return None
        return split_items(line)

    # Time and chance.

    def jiffies(self):
        return (ticks_ms() * 60 // 1000 - self.ti_base) % 5184000  # a day

    def ti_str(self):
        t = self.jiffies() // 60
        return "%02d%02d%02d" % (t // 3600, t // 60 % 60, t % 60)

    def set_ti(self, s):
        """TI$ = "HHMMSS" sets the clock TI and TI$ count from."""
        if len(s) != 6 or not all(_is_digit(c) for c in s):
            raise BasicError(QUANTITY)
        h, m, sec = int(s[:2]), int(s[2:4]), int(s[4:])
        if h > 23 or m > 59 or sec > 59:
            raise BasicError(QUANTITY)
        self.ti_base = ticks_ms() * 60 // 1000 - ((h * 60 + m) * 60 + sec) * 60

    def rnd(self, x):
        if x < 0:
            self.random.seed(int(-x * 1000003) ^ 0x5DEECE66)
        return self.random.next()
