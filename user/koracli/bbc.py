# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's BBC BASIC (project_doc/koracli-spec.md §6.2, docs/koracli.md):
# BBC-inspired, modelled on BBC BASIC V. What a BBC BASIC manual says
# applies; old programs are not promised to run unchanged. A dialect over
# basic.Runtime, as Classic BASIC is (classic.py).
#
# Steps 3b to 3d: LET (with += and -=), PRINT, INPUT, IF THEN ELSE on one
# line or over several (ENDIF), CASE OF WHEN OTHERWISE ENDCASE, REPEAT
# UNTIL, WHILE ENDWHILE, GOTO, GOSUB, RETURN, ON, FOR, NEXT, DIM, READ,
# DATA, RESTORE, END, STOP, REM, SWAP, CLS, CLEAR, TIME=; DEF PROC and FN
# with LOCAL and RETURN parameters, ON ERROR [LOCAL], LOCAL ERROR, RESTORE
# ERROR, ERROR, REPORT; the commands RUN, LIST, NEW and CONT. Star commands
# and files come in 3e.
#
# PROC and FN run their bodies in a loop of their own (run_body), so an FN
# returns its value into the expression that called it; END or STOP inside
# one leaves through basic.Halted, and errors as Python exceptions, past
# each call's LOCAL ERROR handler. A PROC or FN the program does not define
# is the Python function of that name (spec §9.1). An FN's value may be a
# number or a string: its kind is "*", checked when it is used.
#
# A construct over several lines is matched when it runs: lines are stored
# unchecked (spec §4), so the statement that opens one looks ahead, through
# the statements' keywords, for where it goes on (ELSE, WHEN, OTHERWISE)
# and ends (ENDIF, ENDCASE, ENDWHILE), skipping constructs of its kind
# inside it, and remembers what it found until the program changes.
#
# Keywords are capitals only, so a word in small letters is always a name;
# a word in capitals is a keyword only where nothing but a keyword can
# stand (Classic BASIC's rule: at a statement's start unless the word is
# assigned to, and after a value). Abbreviations (P. for PRINT) are taken as
# typed and stored in full. Names are case-sensitive and shared with Python
# one to one (spec §7).
#
# Numbers: integers are Python ints, 32 bits (`%` variables, literals that
# fit, &FF, %101); + and - on integers wrap at 32 bits, * gives a real when
# it does not fit, / and ^ always give reals. Reals are doubles. A variable
# read before it is written is "No such variable", except A% to Z% and @%,
# which always exist. Errors are BBC BASIC V's messages, with its numbers in
# ERR.
#
# The host gives the interpreter what classic.Interpreter takes: write,
# readline, getkey (characters as key_char gives them), the screen's width,
# the Python namespace and command.

import math

import basic
from basic import BasicError, finite, to_int

try:
    from time import ticks_ms, sleep_ms
except ImportError:  # the host tests' MicroPython has no time module
    sleep_ms = None

    def ticks_ms():
        return 0

MISSING = basic.MISSING

# --- Words -------------------------------------------------------------------------

STATEMENTS = (
    "CASE", "CLEAR", "CLS", "CONT", "DATA", "DIM", "ELSE", "END", "ENDCASE", "ENDIF",
    "ENDWHILE", "FOR", "GOSUB", "GOTO", "IF", "INPUT", "LET", "LIST", "NEW", "NEXT", "ON",
    "OTHERWISE", "PRINT", "READ", "REM", "REPEAT", "RESTORE", "RETURN", "RUN", "STOP",
    "SWAP", "UNTIL", "WHEN", "WHILE", "DEF", "ENDPROC", "ERROR", "LOCAL", "REPORT",
)
OPERATORS = ("AND", "OR", "EOR", "DIV", "MOD", "NOT")
CLAUSES = ("THEN", "TO", "STEP", "GOTO", "GOSUB", "ELSE", "LINE", "OF", "OFF")
# Functions taking one operand, with or without brackets (SIN X, LEN A$):
# name -> (argument kind, result kind).
UNARY = {
    "ABS": ("n", "n"), "ACS": ("n", "n"), "ASN": ("n", "n"), "ATN": ("n", "n"),
    "COS": ("n", "n"), "DEG": ("n", "n"), "EXP": ("n", "n"), "INT": ("n", "n"),
    "LN": ("n", "n"), "LOG": ("n", "n"), "RAD": ("n", "n"), "SGN": ("n", "n"),
    "SIN": ("n", "n"), "SQR": ("n", "n"), "TAN": ("n", "n"), "ASC": ("s", "n"),
    "LEN": ("s", "n"), "VAL": ("s", "n"), "CHR$": ("n", "s"), "STR$": ("n", "s"),
    "EVAL": ("s", "n"), "INKEY": ("n", "n"), "INKEY$": ("n", "s"),
}
# Functions with their arguments in brackets: name -> (kinds, how many are
# needed, result kind).
BRACKETED = {
    "LEFT$": ("sn", 1, "s"), "RIGHT$": ("sn", 1, "s"), "MID$": ("snn", 2, "s"),
    "INSTR": ("ssn", 2, "n"), "STRING$": ("ns", 2, "s"),
}
# Values without an argument.
VALUES = ("PI", "TRUE", "FALSE", "GET", "GET$", "POS", "COUNT", "TIME", "RND", "VPOS",
          "ERR", "ERL", "REPORT$")
KEYWORDS = set(STATEMENTS + OPERATORS + CLAUSES + tuple(UNARY) + tuple(BRACKETED) + VALUES +
               ("TAB", "SPC"))
BY_LENGTH = sorted(KEYWORDS, key=lambda k: -len(k))
AFTER_VALUE = ("AND", "OR", "EOR", "DIV", "MOD", "TO", "STEP", "THEN", "ELSE", "GOTO", "GOSUB")

# Abbreviations, as the BBC's manuals list them: the shortest form of each
# keyword, before its full stop. A longer one (PRI.) is the keyword it
# starts, whose shortest form it starts with.
ABBREVIATIONS = {
    "CASE": "CAS", "ENDCASE": "ENDC", "ENDIF": "ENDI", "ENDWHILE": "ENDW",
    "OTHERWISE": "OT", "WHEN": "WHE", "WHILE": "WH",
    "AND": "A", "AUTO": "AU", "CHAIN": "CH", "CHR$": "CHR", "CLEAR": "CL", "CLOSE": "CLO",
    "COLOUR": "C", "COUNT": "COU", "DATA": "D", "DELETE": "DEL", "DRAW": "DR",
    "ELSE": "EL", "ENDPROC": "E", "ENVELOPE": "ENV", "ERROR": "ERR", "EVAL": "EV",
    "FALSE": "FA", "FOR": "F", "GCOL": "GC", "GET$": "GE", "GOSUB": "GOS", "GOTO": "G",
    "HIMEM": "H", "INKEY$": "INK", "INPUT": "I", "INSTR": "INS", "LEFT$": "LE",
    "LINE": "LIN", "LIST": "L", "LOAD": "LO", "LOCAL": "LOC", "LOMEM": "LOM",
    "MID$": "M", "MODE": "MO", "MOVE": "MOV", "NEXT": "N", "OLD": "O", "OSCLI": "OS",
    "PAGE": "PA", "PLOT": "PL", "PRINT": "P", "PROC": "PRO", "READ": "REA",
    "RENUMBER": "REN", "REPEAT": "REP", "REPORT": "REPO", "RESTORE": "RES",
    "RETURN": "R", "RIGHT$": "RI", "SAVE": "SA", "SOUND": "SO", "STEP": "S",
    "STOP": "STO", "STR$": "STR", "STRING$": "STRI", "TAN": "T", "THEN": "TH",
    "TIME": "TI", "TRACE": "TR", "TRUE": "TRU", "UNTIL": "U", "VDU": "V", "VPOS": "VP",
    "WIDTH": "W",
}

# Errors: BBC BASIC V's messages and numbers (ERR).
ERRORS = {
    "No room": 0, "STOP": 0, "Out of range": 1, "Mistake": 4, "Missing ,": 5,
    "Type mismatch": 6, "Missing \"": 9, "Bad DIM": 10, "Array": 14, "Subscript": 15,
    "Syntax error": 16, "Escape": 17, "Division by zero": 18, "Too big": 20,
    "-ve root": 21, "Log range": 22, "Exp range": 24, "No such variable": 26,
    "Missing )": 27, "No such FN/PROC": 29, "Not in a FOR loop": 32, "Can't match FOR": 33,
    "FOR variable": 34, "No TO": 36, "Not in a subroutine": 38, "ON syntax": 39,
    "ON range": 40, "No such line": 41, "Out of DATA": 42, "Not in a REPEAT loop": 43,
    "Not in a WHILE loop": 46, "Missing ENDCASE": 47, "Missing OF": 48, "Missing ENDIF": 49,
    "Not in a function": 7, "Not LOCAL": 12, "Not in a procedure": 13, "Arguments": 31,
}
# The runtime's errors (Classic BASIC's names) in BBC BASIC's words.
FROM_RUNTIME = {
    basic.SYNTAX: "Syntax error", basic.TYPE: "Type mismatch", basic.QUANTITY: "Too big",
    basic.UNDEF_LINE: "No such line", basic.UNDEF_FN: "No such FN/PROC", "ESCAPE": "Escape",
    "NEXT WITHOUT FOR": "Not in a FOR loop", "RETURN WITHOUT GOSUB": "Not in a subroutine",
    "WHILE WITHOUT WEND": "Missing ENDWHILE", "WEND WITHOUT WHILE": "Not in a WHILE loop",
    "OUT OF DATA": "Out of DATA", "DIVISION BY ZERO": "Division by zero",
    "OVERFLOW": "Too big", "OUT OF MEMORY": "No room", "REDIM'D ARRAY": "Bad DIM",
    "BAD SUBSCRIPT": "Subscript", "CAN'T CONTINUE": "Can't continue",
}

MISTAKE = "Mistake"
SYNTAX = "Syntax error"
TYPE = "Type mismatch"
TOO_BIG = "Too big"

# @%: the format numbers print in, &00AABBCC: AA 0 general, 1 exponent, 2
# fixed; BB the digits; CC the field's width. STR$ uses it with bit 24 set.
AT_DEFAULT = 0x90A


def _is_static(key):
    """A% to Z% and @%: always there, kept by RUN and CLEAR."""
    return len(key) == 2 and key[1] == "%" and ("A" <= key[0] <= "Z" or key[0] == "@")


# --- Lexing -------------------------------------------------------------------------
#
# Tokens are (kind, value, start, end), as Classic BASIC's: "W" a word (a
# letter or _, then letters, digits and _, then $ or %; or @%), "N" a number
# (an int or a float), "S" a string, "O" an operator or punctuation, "R" the
# raw text after REM or DATA.

def _is_letter(c):
    return "A" <= c <= "Z" or "a" <= c <= "z" or c == "_"


def _is_digit(c):
    return "0" <= c <= "9"


def _int32(n):
    n &= 0xFFFFFFFF
    return n - 0x100000000 if n & 0x80000000 else n


def _number(text, i):
    """A number at `i`: (value, end), an int when it is whole, written
    without a point or an exponent, and fits in 32 bits."""
    n = len(text)
    j = i
    while j < n and _is_digit(text[j]):
        j += 1
    real = False
    if j < n and text[j] == ".":
        real = True
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
            real = True
    s = text[i:j]
    if not real:
        v = int(s)
        if v <= 2147483647:
            return v, j
    return float(s), j


def _radix(text, i, digits, base):
    """&FF or %101 at `i`: (value, end), 32 bits; None without digits."""
    j = i + 1
    while j < len(text) and text[j] in digits:
        j += 1
    if j == i + 1:
        return None
    return _int32(int(text[i + 1:j], base)), j


def _word_end(text, i):
    n = len(text)
    j = i + 1
    while j < n and (_is_letter(text[j]) or _is_digit(text[j])):
        j += 1
    if j < n and text[j] in "$%":
        j += 1
    return j


def _string_end(text, i):
    """The end of a string starting at `i`, past its closing quote ("" is a
    quote inside it), and its value; None when it is not closed."""
    j = i + 1
    out = []
    n = len(text)
    while j < n:
        c = text[j]
        if c == '"':
            if j + 1 < n and text[j + 1] == '"':
                out.append('"')
                j += 2
                continue
            return j + 1, "".join(out)
        out.append(c)
        j += 1
    return None


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
            s = _string_end(text, i)
            if s is None:
                raise BasicError('Missing "')
            toks.append(("S", s[1], i, s[0]))
            i = s[0]
            start = False
            continue
        if start and text.startswith("REM", i):
            toks.append(("W", "REM", i, i + 3))
            toks.append(("R", text[i + 3:], i + 3, n))
            break
        if start and text.startswith("DATA", i):
            toks.append(("W", "DATA", i, i + 4))
            toks.append(("R", text[i + 4:], i + 4, n))  # to the line's end, as on a BBC
            break
        if _is_digit(c) or (c == "." and i + 1 < n and _is_digit(text[i + 1])):
            value, j = _number(text, i)
            toks.append(("N", value, i, j))
            i = j
            start = False
            continue
        if c == "&" or c == "%":
            r = _radix(text, i, "0123456789abcdefABCDEF", 16) if c == "&" else \
                _radix(text, i, "01", 2)
            if r is not None:
                toks.append(("N", r[0], i, r[1]))
                i = r[1]
                start = False
                continue
        if c == "@" and i + 1 < n and text[i + 1] == "%":
            toks.append(("W", "@%", i, i + 2))
            i += 2
            start = False
            continue
        if _is_letter(c):
            j = _word_end(text, i)
            word = text[i:j]
            toks.append(("W", word, i, j))
            i = j
            start = word in ("THEN", "ELSE")
            continue
        three = text[i:i + 3]
        if three == ">>>":
            toks.append(("O", three, i, i + 3))
            i += 3
            start = False
            continue
        two = text[i:i + 2]
        if two in ("<=", ">=", "<>", "<<", ">>", "+=", "-="):
            toks.append(("O", two, i, i + 2))
            i += 2
            start = False
            continue
        toks.append(("O", c, i, i + 1))
        start = c == ":"
        i += 1
    return toks


def _keyword_prefix(word, allowed):
    """The longest keyword of `allowed` that `word` starts with, or None."""
    for k in BY_LENGTH:
        if k in allowed and word.startswith(k):
            return k
    return None


def _abbreviation(prefix):
    """The keyword `prefix` (capitals, before a full stop) stands for, or
    None: the one it starts, whose shortest form it starts with (the
    longest such shortest form, if there are more)."""
    best = None
    for k, short in ABBREVIATIONS.items():
        if k.startswith(prefix) and prefix.startswith(short):
            if best is None or len(short) > len(ABBREVIATIONS[best]):
                best = k
    return best


def normalize(text):
    """A line as stored: its abbreviations written in full (P. is PRINT),
    the rest as typed. Inside strings, REM and DATA nothing changes."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"':
            s = _string_end(text, i)
            j = n if s is None else s[0]
            out.append(text[i:j])
            i = j
            continue
        if "A" <= c <= "Z" and (i == 0 or not (_is_letter(text[i - 1]) or
                                              _is_digit(text[i - 1]))):
            j = i
            while j < n and "A" <= text[j] <= "Z":
                j += 1
            word = text[i:j]
            if j < n and text[j] == ".":  # names have no full stops: G.20 is GOTO20
                k = _abbreviation(word)
                if k is not None:
                    out.append(k)
                    i = j + 1
                    if k in ("REM", "DATA"):
                        out.append(text[i:])
                        break
                    continue
            out.append(word)
            i = j
            if word in ("REM", "DATA"):
                out.append(text[i:])
                break
            continue
        out.append(c)
        i += 1
    return "".join(out)


# --- Numbers -------------------------------------------------------------------------

def _strip(s):
    if "." in s:
        s = s.rstrip("0").rstrip(".")
    return s


def _exp(x, digits):
    """`x` to `digits` significant digits: the mantissa's text and the
    exponent."""
    mant, e = (("%." + str(digits - 1) + "e") % x).split("e")
    return mant, int(e)


def num_format(x, at=AT_DEFAULT):
    """A number as PRINT shows it with @% = `at` (without the field)."""
    kind = (at >> 16) & 0xFF
    digits = (at >> 8) & 0xFF
    if type(x) is int and kind == 0:
        return str(x)  # integers, whole, in the general format
    x = float(x)
    if kind == 2:  # fixed: `digits` decimals
        return ("%." + str(min(digits, 10)) + "f") % x
    if digits == 0 or digits > 10:
        digits = 10
    if kind == 1:  # exponent: `digits` significant digits
        mant, e = _exp(x, digits)
        return "%sE%d" % (mant, e)
    if x == 0:
        return "0"
    mant, e = _exp(x, digits)
    if e >= digits or e < -1:  # 0.1 is the smallest without an exponent
        return "%sE%d" % (_strip(mant), e)
    return _strip(("%." + str(max(digits - 1 - e, 0)) + "f") % x)


def num_str(x, at=AT_DEFAULT):
    """STR$: nine digits, unless bit 24 of @% says to use @%."""
    return num_format(x, at if at & 0x1000000 else AT_DEFAULT)


def _number_prefix(s):
    """The number at the start of `s` (spaces and a sign allowed), as VAL
    reads it: 0 when there is none."""
    i = 0
    n = len(s)
    while i < n and s[i] == " ":
        i += 1
    sign = 1
    if i < n and s[i] in "+-":
        if s[i] == "-":
            sign = -1
        i += 1
    if i < n and (_is_digit(s[i]) or (s[i] == "." and i + 1 < n and _is_digit(s[i + 1]))):
        v, _ = _number(s, i)
        return -v if sign < 0 else v
    return 0


def val(s):
    return _number_prefix(s)


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
            s = _string_end(raw, i)
            if s is None:
                items.append((raw[i + 1:], True))
                return items
            items.append((s[1], True))
            i = raw.find(",", s[0])
            if i < 0:
                return items
        else:
            j = raw.find(",", i)
            end = n if j < 0 else j
            items.append((raw[i:end].rstrip(), False))
            if j < 0:
                return items
            i = j
        i += 1


def key_char(ch, key):
    """A key as GET and INKEY give it: its character, or a BBC's codes for
    Return, Delete and the cursor keys."""
    import keys
    codes = {keys.ENTER: 13, keys.KP_ENTER: 13, keys.BACKSPACE: 127, keys.DELETE: 127,
             keys.LEFT: 136, keys.RIGHT: 137, keys.DOWN: 138, keys.UP: 139}
    if key in codes:
        return chr(codes[key])
    return ch or ""


def _int(v):
    """A number as an integer, for the integer operators: toward zero, 32
    bits."""
    if type(v) is int:
        return v
    try:
        return to_int(v)
    except BasicError:
        raise BasicError(TOO_BIG)


def _add(a, b):
    def add():
        r = a() + b()
        if type(r) is int:
            return r if -2147483648 <= r <= 2147483647 else _int32(r)
        return finite(r)
    return add


def _sub(a, b):
    def sub():
        r = a() - b()
        if type(r) is int:
            return r if -2147483648 <= r <= 2147483647 else _int32(r)
        return finite(r)
    return sub


def _mul(a, b):
    def mul():
        r = a() * b()
        if type(r) is int:
            return r if -2147483648 <= r <= 2147483647 else float(r)
        return finite(r)
    return mul


def _div(a, b):
    return lambda: finite(float(a()) / float(b()))


def _idiv(x, y):
    x, y = _int(x), _int(y)
    if y == 0:
        raise ZeroDivisionError
    q = abs(x) // abs(y)
    return _int32(q if (x < 0) == (y < 0) else -q)


def _imod(x, y):
    q = _idiv(x, y)
    return _int32(_int(x) - q * _int(y))


def _power(a, b):
    def power():
        try:
            return finite(math.pow(a(), b()))
        except ValueError:
            raise BasicError("Log range")
    return power


def _compare(op, a, b):
    if op == "=":
        return lambda: -1 if a() == b() else 0
    if op == "<>":
        return lambda: -1 if a() != b() else 0
    if op == "<":
        return lambda: -1 if a() < b() else 0
    if op == ">":
        return lambda: -1 if a() > b() else 0
    if op == "<=":
        return lambda: -1 if a() <= b() else 0
    return lambda: -1 if a() >= b() else 0


def _shift(op, a, b):
    def shift():
        x, n = _int(a()), _int(b())
        if op == "<<":
            return _int32(x << n) if 0 <= n < 32 else 0
        if op == ">>":
            return x >> n if 0 <= n < 32 else (-1 if x < 0 else 0)
        return _int32((x & 0xFFFFFFFF) >> n) if 0 <= n < 32 else 0
    return shift


def _count(v):
    n = _int(v)
    return n if n > 0 else 0


def _want(f, k, want):
    """`f` (of kind `k`) where a value of kind `want` must be: as it is, or
    checked when it runs for an FN's value (kind "*")."""
    if k == want:
        return f
    if k != "*":
        raise BasicError(TYPE)
    if want == "s":
        def string():
            v = f()
            if type(v) is not str:
                raise BasicError(TYPE)
            return v
        return string

    def number():
        v = f()
        if type(v) is str:
            raise BasicError(TYPE)
        return v
    return number


def _same_kind(v, w):
    if (type(v) is str) != (type(w) is str):
        raise BasicError(TYPE)


def _call_name(word, prefix):
    """PROCname's or FNname's name, or None for another word."""
    if word.startswith(prefix) and len(word) > len(prefix):
        return word[len(prefix):]
    return None


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
            raise BasicError({")": "Missing )", ",": "Missing ,"}.get(value, SYNTAX))
        self.pos += 1

    def is_word(self, value):
        t = self.peek()
        return t is not None and t[0] == "W" and t[1] == value

    def word_kw(self, allowed):
        """The keyword of `allowed` the next word is or starts with (split
        off from the rest of the word), or None."""
        t = self.peek()
        if t is None or t[0] != "W":
            return None
        if t[1] in allowed:
            return t[1]
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

    def at_end(self, in_then=False):
        t = self.peek()
        if t is None or (t[0] == "O" and t[1] == ":"):
            return True
        return in_then and t[0] == "W" and t[1] == "ELSE"

    def line_number(self):
        t = self.next()
        if t[0] != "N" or type(t[1]) is not int or t[1] < 0:
            raise BasicError(SYNTAX)
        return t[1]

    # Expressions: (closure, kind), kind "n" or "s". Highest first: unary
    # -, +, NOT and the functions; ^; * / DIV MOD; + -; the comparisons and
    # shifts; AND; OR EOR.

    def num(self):
        f, k = self.expr()
        return _want(f, k, "n")

    def string(self):
        f, k = self.expr()
        return _want(f, k, "s")

    def expr(self):
        f, k = self.and_()
        while True:
            op = self.word_kw(("OR", "EOR"))
            if op is None:
                return f, k
            self.pos += 1
            g, k2 = self.and_()
            f, g = _want(f, k, "n"), _want(g, k2, "n")
            if op == "OR":
                f = (lambda a, b: lambda: _int(a()) | _int(b()))(f, g)
            else:
                f = (lambda a, b: lambda: _int(a()) ^ _int(b()))(f, g)

    def and_(self):
        f, k = self.relation()
        while self.word_kw(("AND",)):
            self.pos += 1
            g, k2 = self.relation()
            f, g = _want(f, k, "n"), _want(g, k2, "n")
            f = (lambda a, b: lambda: _int(a()) & _int(b()))(f, g)
        return f, k

    def relation(self):
        f, k = self.add()
        while True:
            t = self.peek()
            if t is None or t[0] != "O" or t[1] not in ("=", "<", ">", "<=", ">=", "<>",
                                                         "<<", ">>", ">>>"):
                return f, k
            self.pos += 1
            g, k2 = self.add()
            if t[1] in ("<<", ">>", ">>>"):
                f, g = _want(f, k, "n"), _want(g, k2, "n")
                f = _shift(t[1], f, g)
                k = "n"
                continue
            if k == "*" or k2 == "*":
                f, k = _compare_checked(t[1], f, g), "n"
                continue
            if k != k2:
                raise BasicError(TYPE)
            f, k = _compare(t[1], f, g), "n"

    def add(self):
        f, k = self.mul()
        while True:
            if self.is_op("+"):
                self.pos += 1
                g, k2 = self.mul()
                if "s" in (k, k2):
                    f, g = _want(f, k, "s"), _want(g, k2, "s")
                    f, k = (lambda a, b: lambda: a() + b())(f, g), "s"
                elif "n" in (k, k2):
                    f, g = _want(f, k, "n"), _want(g, k2, "n")
                    f, k = _add(f, g), "n"
                else:  # two FNs' values: numbers or strings, as they come
                    f, k = _add_checked(f, g), "*"
            elif self.is_op("-"):
                self.pos += 1
                g, k2 = self.mul()
                f, g = _want(f, k, "n"), _want(g, k2, "n")
                f = _sub(f, g)
            else:
                return f, k

    def mul(self):
        f, k = self.power()
        while True:
            if self.is_op("*"):
                op = "*"
            elif self.is_op("/"):
                op = "/"
            else:
                op = self.word_kw(("DIV", "MOD"))
                if op is None:
                    return f, k
            self.pos += 1
            g, k2 = self.power()
            f, g = _want(f, k, "n"), _want(g, k2, "n")
            if op == "*":
                f = _mul(f, g)
            elif op == "/":
                f = _div(f, g)
            elif op == "DIV":
                f = (lambda a, b: lambda: _idiv(a(), b()))(f, g)
            else:
                f = (lambda a, b: lambda: _imod(a(), b()))(f, g)

    def power(self):
        f, k = self.unary()
        while self.is_op("^"):
            self.pos += 1
            g, k2 = self.unary()
            f, g = _want(f, k, "n"), _want(g, k2, "n")
            f = _power(f, g)
        return f, k

    def unary(self):
        if self.is_op("-"):
            self.pos += 1
            f, k = self.unary()
            f = _want(f, k, "n")

            def neg(a=f):
                v = a()
                return _int32(-v) if type(v) is int else -v
            return neg, "n"
        if self.is_op("+"):
            self.pos += 1
            f, k = self.unary()
            f = _want(f, k, "n")
            return f, "n"
        if self.is_word("NOT"):
            self.pos += 1
            f, k = self.unary()
            f = _want(f, k, "n")
            return (lambda a: lambda: ~_int(a()))(f), "n"
        return self.atom()

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
        rt = self.rt
        if value in UNARY:
            return self.unary_function(value)
        if value in BRACKETED:
            return self.bracketed(value)
        if value == "PI":
            return (lambda: math.pi), "n"
        if value == "TRUE":
            return (lambda: -1), "n"
        if value == "FALSE":
            return (lambda: 0), "n"
        if value == "GET":
            return (lambda: ord((rt.wait_key(None) or "\0")[0])), "n"
        if value == "GET$":
            return (lambda: rt.wait_key(None)[:1]), "s"
        if value in ("POS", "COUNT"):
            return (lambda: rt.out.col), "n"
        if value == "VPOS":
            return (lambda: 0), "n"
        if value == "TIME":
            return rt.time, "n"
        if value == "RND":
            if self.is_op("("):
                self.pos += 1
                f = self.num()
                self.expect_op(")")
                return (lambda: rt.rnd(f())), "n"
            return (lambda: rt.rnd(None)), "n"
        if value == "ERR":
            return (lambda: rt.err), "n"
        if value == "ERL":
            return (lambda: rt.erl), "n"
        if value == "REPORT$":
            return (lambda: rt.report_text), "s"
        name = _call_name(value, "FN")
        if name is not None:
            args = self.call_args()
            return (lambda: rt.call("FN", name, args)), "*"
        if value in KEYWORDS:
            raise BasicError(SYNTAX)
        self.pos -= 1
        get, _, kind = self.variable()
        return get, kind

    def call_args(self):
        """A call's arguments, in brackets if there are any: (closure, kind,
        setter) each, the setter a variable's (for RETURN parameters) or
        None."""
        args = []
        if not self.is_op("("):
            return args
        self.pos += 1
        while True:
            t = self.peek()
            nxt = self.toks[self.pos + 1] if self.pos + 1 < len(self.toks) else None
            if t is not None and t[0] == "W" and t[1] not in KEYWORDS and \
                    nxt is not None and nxt[0] == "O" and nxt[1] in (",", ")") and \
                    _call_name(t[1], "FN") is None:
                get, set_, kind = self.variable()
                args.append((get, kind, set_))
            else:
                f, kind = self.expr()
                args.append((f, kind, None))
            if self.is_op(","):
                self.pos += 1
                continue
            self.expect_op(")")
            return args

    def unary_function(self, name):
        want, result = UNARY[name]
        if name == "STR$" and self.is_op("~"):
            self.pos += 1
            f, k = self.unary()
            f = _want(f, k, "n")
            return (lambda: "%X" % (_int(f()) & 0xFFFFFFFF)), "s"
        f, k = self.unary()
        f = _want(f, k, want)
        rt = self.rt
        if name == "EVAL":
            return (lambda: rt.eval(f())), "n"
        return _unary(name, f, rt), result

    def bracketed(self, name):
        kinds, needed, result = BRACKETED[name]
        self.expect_op("(")
        a = [self.expr()]
        while self.is_op(","):
            self.pos += 1
            a.append(self.expr())
        self.expect_op(")")
        if not needed <= len(a) <= len(kinds):
            raise BasicError(SYNTAX)
        fs = [_want(f, k, want) for (f, k), want in zip(a, kinds)]
        return _bracketed(name, fs), result

    def variable(self):
        """A variable or array element: (getter, setter, kind). The setter
        takes a value of the right kind and converts it to the variable's."""
        t = self.next()
        if t[0] != "W":
            raise BasicError(SYNTAX)
        key = t[1]
        if key in KEYWORDS or _call_name(key, "FN") is not None or \
                _call_name(key, "PROC") is not None:
            raise BasicError(SYNTAX)
        kind = "s" if key.endswith("$") else "n"
        rt = self.rt
        if key == "@%":
            return rt.get_at, rt.set_at, "n"
        if self.is_op("("):
            self.pos += 1
            idx = [self.num()]
            while self.is_op(","):
                self.pos += 1
                idx.append(self.num())
            self.expect_op(")")
            return _element(rt, key, idx) + (kind,)
        get, set_ = rt.scalar(key, basic.base_name(key))
        return get, set_, kind

    # Statements.

    def statements(self):
        out, kinds = [], []
        self.stmt_list(out, kinds, False)
        if self.peek() is not None:
            raise BasicError(MISTAKE)
        return out, kinds

    def stmt_list(self, out, kinds, in_then):
        """Statements to the line's end, or (`in_then`) to an ELSE."""
        while True:
            if self.peek() is None or (in_then and self.is_word("ELSE")):
                return
            if self.is_op(":"):
                self.pos += 1
                continue
            word = self.statement(out, kinds, in_then)
            if self.peek() is None or (in_then and self.is_word("ELSE")):
                return
            if word in GLUED:
                continue  # REPEAT UNTIL GET, OTHERWISE PRINT: no colon needed
            if not self.is_op(":"):
                raise BasicError(SYNTAX)
            self.pos += 1

    def statement(self, out, kinds, in_then):
        t = self.peek()
        rt = self.rt
        if t[0] == "O" and t[1] == "=":
            # An FN's value, ending its body.
            self.pos += 1
            f, k = self.expr()
            out.append(lambda: rt.fn_return(f()))
            kinds.append("=")
            return "="
        if t[0] != "W":
            raise BasicError(MISTAKE)
        word = t[1]
        name = _call_name(word, "PROC")
        if name is not None:
            self.pos += 1
            args = self.call_args()

            def proc():
                rt.call("PROC", name, args)
            out.append(proc)
            kinds.append("PROC")
            return "PROC"
        if word not in STATEMENTS:
            # An assignment, or a keyword without its space (PRINTI, FORI=1):
            # the assignment is tried first, so names may start with one.
            error = BasicError(MISTAKE)
            if word == "TIME" or word not in KEYWORDS:
                pos, toks = self.pos, list(self.toks)
                try:
                    f = self.assignment()
                    if self.at_end(in_then):
                        out.append(f)
                        kinds.append(None)
                        return None
                except BasicError as e:
                    if word in ("TIME", "@%") or self.pos > pos + 1:
                        error = e  # it was an assignment, with a mistake
                self.pos, self.toks = pos, toks
            k = _keyword_prefix(word, STATEMENTS)
            if k is None:
                raise error
            self.split(k)
            word = k
        self.pos += 1
        before = len(out)
        getattr(self, "s_" + word)(out, kinds, in_then)
        if len(kinds) == before and len(out) > before:
            kinds.append(word)
        while len(kinds) < len(out):
            kinds.append(None)
        return word

    def assignment(self):
        rt = self.rt
        if self.is_word("TIME"):
            self.pos += 1
            self.expect_op("=")
            f = self.num()
            return lambda: rt.set_time(f())
        get, set_, kind = self.variable()
        op = self.next()
        if op[0] != "O" or op[1] not in ("=", "+=", "-="):
            raise BasicError(MISTAKE)
        f, k = self.expr()
        f = _want(f, k, kind)
        if op[1] == "=":
            return lambda: set_(f())
        if op[1] == "-=":
            if kind == "s":
                raise BasicError(TYPE)
            g = _sub(get, f)
        elif kind == "s":
            g = (lambda a, b: lambda: a() + b())(get, f)
        else:
            g = _add(get, f)
        return lambda: set_(g())

    def s_LET(self, out, kinds, in_then):
        out.append(self.assignment())

    def s_REM(self, out, kinds, in_then):
        self.pos = len(self.toks)  # a remark ends the line, ELSE and all
        out.append(lambda: None)

    def s_DATA(self, out, kinds, in_then):
        self.pos = len(self.toks)
        out.append(lambda: None)

    def s_PRINT(self, out, kinds, in_then):
        parts = []
        while not self.at_end(in_then):
            t = self.peek()
            if t[0] == "O" and t[1] in (";", ",", "'", "~"):
                self.pos += 1
                parts.append((t[1], None))
                continue
            if t[0] == "W" and t[1] in ("TAB", "SPC") and \
                    self.pos + 1 < len(self.toks) and self.toks[self.pos + 1][1] == "(":
                self.pos += 2
                f = self.num()
                g = None
                if t[1] == "TAB" and self.is_op(","):
                    self.pos += 1
                    g = self.num()
                self.expect_op(")")
                parts.append((t[1], (f, g)))
                continue
            f, k = self.expr()
            parts.append((k, f))
        out.append(_print(self.rt, parts))

    def s_GOTO(self, out, kinds, in_then):
        f = self.num()
        rt = self.rt
        out.append(lambda: rt.goto(_int(f())))

    def s_GOSUB(self, out, kinds, in_then):
        f = self.num()
        rt = self.rt

        def gosub():
            n = _int(f())
            rt.push_gosub()
            return rt.goto(n)
        out.append(gosub)

    def s_RETURN(self, out, kinds, in_then):
        out.append(self.rt.do_return)

    def s_END(self, out, kinds, in_then):
        rt = self.rt
        out.append(lambda: rt.halt("END"))

    def s_STOP(self, out, kinds, in_then):
        rt = self.rt
        out.append(lambda: rt.halt("STOP"))

    def branch(self, out, kinds):
        """What THEN or ELSE leads to: a line number (GOTO) or statements."""
        t = self.peek()
        if t is not None and t[0] == "N":
            n = self.line_number()
            rt = self.rt
            out.append(lambda: rt.goto(n))
        else:
            self.stmt_list(out, kinds, True)
        while len(kinds) < len(out):
            kinds.append(None)

    def s_IF(self, out, kinds, in_then):
        cond = self.num()
        then = self.take_kw(("THEN",)) is not None  # optional, as on a BBC
        if then and self.peek() is None and not in_then:
            # THEN ending the line: IF over several lines, to ELSE and ENDIF.
            rt = self.rt

            def if_block():
                if cond() != 0:
                    return None
                rt.li, rt.si = rt.block_end(BLOCK_IF, ("ELSE",), "Missing ENDIF")
                return True
            out.append(if_block)
            kinds.append("IF THEN")
            return
        skip = [0]
        out.append(_if(self.rt, cond, skip))
        kinds.append("IF")
        self.branch(out, kinds)
        if self.is_word("ELSE"):
            self.pos += 1
            end = [0]
            out.append(_skip(self.rt, end))
            kinds.append(None)
            skip[0] = len(out)
            self.branch(out, kinds)
            end[0] = len(out)
        else:
            skip[0] = -1  # the rest of the line

    def s_ELSE(self, out, kinds, in_then):
        # An ELSE no IF on its line took: a multi-line IF's, reached at the
        # end of the part for a true condition, which goes on after ENDIF.
        rt = self.rt

        def else_():
            rt.li, rt.si = rt.block_end(BLOCK_IF, (), "Missing ENDIF")
            return True
        out.append(else_)

    def s_ENDIF(self, out, kinds, in_then):
        out.append(lambda: None)

    def s_CASE(self, out, kinds, in_then):
        f, k = self.expr()
        if self.take_kw(("OF",)) is None:
            raise BasicError("Missing OF")
        if self.peek() is not None:
            raise BasicError(SYNTAX)  # OF ends the line; the WHENs follow
        rt = self.rt
        out.append(lambda: rt.do_case(f(), k))

    def s_WHEN(self, out, kinds, in_then):
        values = [self.expr()]
        while self.is_op(","):
            self.pos += 1
            values.append(self.expr())
        out.append(self.rt.case_end)  # the end of the clause before: on after ENDCASE
        kinds.append(("WHEN", values))

    def s_OTHERWISE(self, out, kinds, in_then):
        out.append(self.rt.case_end)

    def s_ENDCASE(self, out, kinds, in_then):
        out.append(lambda: None)

    def s_REPEAT(self, out, kinds, in_then):
        out.append(self.rt.push_repeat)

    def s_UNTIL(self, out, kinds, in_then):
        cond = self.num()
        rt = self.rt
        out.append(lambda: rt.do_until(cond))

    def s_WHILE(self, out, kinds, in_then):
        cond = self.num()
        rt = self.rt
        out.append(lambda: rt.do_while(cond))

    def s_ENDWHILE(self, out, kinds, in_then):
        out.append(self.rt.do_wend)

    def s_FOR(self, out, kinds, in_then):
        t = self.peek()
        if t is None or t[0] != "W" or (self.pos + 1 < len(self.toks) and
                                         self.toks[self.pos + 1][1] == "("):
            raise BasicError("FOR variable")
        key = t[1]
        get, set_, kind = self.variable()
        if kind != "n" or key == "@%":
            raise BasicError("FOR variable")
        self.expect_op("=")
        start = self.num()
        if self.take_kw(("TO",)) is None:
            raise BasicError("No TO")
        limit = self.num()
        step = None
        if self.take_kw(("STEP",)) is not None:
            step = self.num()
        rt = self.rt

        def for_():
            set_(start())
            rt.push_for(key, limit(), step() if step is not None else 1)
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
        if t[0] != "W" or t[1] in KEYWORDS:
            raise BasicError(SYNTAX)
        return t[1]

    def s_ON(self, out, kinds, in_then):
        if self.is_word("ERROR"):
            self.pos += 1
            return self.on_error(out, kinds)
        sel = self.num()
        how = self.take_kw(("GOTO", "GOSUB"))
        if how is None:
            raise BasicError("ON syntax")
        targets = [self.num()]
        while self.is_op(","):
            self.pos += 1
            targets.append(self.num())
        has_else = False
        if self.is_word("ELSE"):
            self.pos += 1
            has_else = True
        rt = self.rt

        def on():
            n = _int(sel())
            if n < 1 or n > len(targets):
                if has_else:
                    return None  # on to the statements after ELSE
                raise BasicError("ON range")
            line = _int(targets[n - 1]())
            if how == "GOSUB":
                # Back to after the ON statement: past its ELSE's statements.
                rt.push_gosub()
                if has_else:
                    rt.stack[-1] = ("G", rt.li, 1 << 30)
            return rt.goto(line)
        out.append(on)
        if has_else:
            kinds.append("ON")
            self.stmt_list(out, kinds, False)

    def s_DIM(self, out, kinds, in_then):
        decls = []
        while True:
            t = self.next()
            if t[0] != "W" or t[1] in KEYWORDS or t[1] == "@%":
                raise BasicError("Bad DIM")
            key = t[1]
            self.expect_op("(")
            dims = [self.num()]
            while self.is_op(","):
                self.pos += 1
                dims.append(self.num())
            self.expect_op(")")
            decls.append((key, dims))
            if not self.is_op(","):
                break
            self.pos += 1
        rt = self.rt

        def dim():
            for key, dims in decls:
                sizes = [_int(d()) for d in dims]
                if any(n < 0 for n in sizes):
                    raise BasicError("Bad DIM")
                rt.dim(key, sizes, basic.base_name(key))
        out.append(dim)

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
                elif quoted:
                    raise BasicError(TYPE, line)
                else:
                    set_(val(text))
        out.append(read)

    def s_RESTORE(self, out, kinds, in_then):
        if self.is_word("ERROR"):
            self.pos += 1
            out.append(self.rt.restore_error)
            return
        line = None
        if not self.at_end(in_then):
            line = self.line_number()
        rt = self.rt
        out.append(lambda: rt.restore(line))

    def s_INPUT(self, out, kinds, in_then):
        whole = self.take_kw(("LINE",)) is not None
        items = []
        while not self.at_end(in_then):
            t = self.peek()
            if t[0] == "S":
                self.pos += 1
                ask = self.is_op(",") or self.is_op(";")
                if ask:
                    self.pos += 1
                items.append(("p", t[1] + ("?" if ask else "")))
                continue
            if self.is_op(",") or self.is_op(";"):
                self.pos += 1
                continue
            items.append(("v", self.variable()))
        if not any(kind == "v" for kind, _ in items):
            raise BasicError(SYNTAX)
        rt = self.rt
        out.append(lambda: rt.do_input(items, whole))

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

    def s_CLEAR(self, out, kinds, in_then):
        out.append(self.rt.clear)

    def s_CLS(self, out, kinds, in_then):
        rt = self.rt

        def cls():
            rt.out.write("\x1b[2J\x1b[H")
            rt.out.col = 0
        out.append(cls)

    def s_RUN(self, out, kinds, in_then):
        line = None if self.at_end(in_then) else self.line_number()
        rt = self.rt
        out.append(lambda: rt.do_run(line))

    def on_error(self, out, kinds):
        """ON ERROR statements, ON ERROR LOCAL statements, ON ERROR OFF. The
        statements are the rest of the line, run when an error comes; run
        here, ON ERROR only sets them as the handler."""
        rt = self.rt
        if self.take_kw(("OFF",)) is not None:
            out.append(rt.error_off)
            return
        local = self.take_kw(("LOCAL",)) is not None
        start = len(out) + 1

        def on_error():
            rt.set_handler(start, local)
            rt.si = 1 << 30  # the handler's statements wait for an error
            return True
        out.append(on_error)
        kinds.append("ON ERROR")
        self.stmt_list(out, kinds, False)

    def s_DEF(self, out, kinds, in_then):
        """DEF PROCname(params) or DEF FNname(params) [= expression]: the
        header of a body that starts after it. Reached as the program runs,
        DEF skips its line, as REM does."""
        t = self.next()
        if t[0] != "W":
            raise BasicError(SYNTAX)
        kind = "PROC" if _call_name(t[1], "PROC") is not None else "FN"
        name = _call_name(t[1], kind)
        if name is None:
            raise BasicError(SYNTAX)
        params = []
        if self.is_op("("):
            self.pos += 1
            while True:
                ret = self.take_kw(("RETURN",)) is not None
                p = self.next()
                if p[0] != "W" or p[1] in KEYWORDS or p[1] == "@%" or self.is_op("("):
                    raise BasicError(SYNTAX)
                params.append((p[1], ret))
                if not self.is_op(","):
                    break
                self.pos += 1
            self.expect_op(")")
        value = None
        if kind == "FN" and self.is_op("="):
            self.pos += 1
            value = self.expr()[0]  # a one-line FN
        rt = self.rt

        def skip():
            rt.si = 1 << 30
            return True
        out.append(skip)
        kinds.append(("DEF", kind, name, params, value))

    def s_ENDPROC(self, out, kinds, in_then):
        out.append(self.rt.end_proc)

    def s_LOCAL(self, out, kinds, in_then):
        rt = self.rt
        if self.take_kw(("ERROR",)) is not None:
            out.append(rt.local_error)
            return
        keys = [self.next_name()]
        while self.is_op(","):
            self.pos += 1
            keys.append(self.next_name())
        out.append(lambda: rt.do_local(keys))

    def s_ERROR(self, out, kinds, in_then):
        """ERROR number, message: an error of the program's own."""
        number = self.num()
        self.expect_op(",")
        text = self.string()

        def error():
            e = BasicError(text())
            e.number = _int(number())
            raise e
        out.append(error)

    def s_REPORT(self, out, kinds, in_then):
        rt = self.rt
        out.append(lambda: rt.out.write(rt.report_text))

    def s_LIST(self, out, kinds, in_then):
        """LIST, LIST 20, LIST 20,50, LIST 20, and LIST ,50."""
        low, high = 0, basic.MAX_LINE
        if not self.at_end(in_then):
            if not self.is_op(","):
                low = high = self.line_number()
            if self.is_op(","):
                self.pos += 1
                high = basic.MAX_LINE if self.at_end(in_then) else self.line_number()
        rt = self.rt
        out.append(lambda: rt.do_list(low, high))

    def s_NEW(self, out, kinds, in_then):
        out.append(self.rt.do_new)

    def s_CONT(self, out, kinds, in_then):
        out.append(self.rt.do_cont)


# The constructs over several lines: (the keywords that open one, those
# that end it), as `Interpreter.scan` takes them.
BLOCK_IF = (("IF THEN",), ("ENDIF",))
BLOCK_CASE = (("CASE",), ("ENDCASE",))
# The statements another may follow without a colon.
GLUED = ("REPEAT", "WHEN", "OTHERWISE", "ELSE")


def _compare_checked(op, a, b):
    """A comparison with an FN's value: both numbers or both strings."""
    c = _compare(op, lambda: x[0], lambda: x[1])
    x = [None, None]

    def compare():
        v, w = a(), b()
        _same_kind(v, w)
        x[0], x[1] = v, w
        return c()
    return compare


def _add_checked(a, b):
    """+ on two FNs' values: strings join, numbers add."""
    add = _add(lambda: x[0], lambda: x[1])
    x = [None, None]

    def plus():
        v, w = a(), b()
        _same_kind(v, w)
        if type(v) is str:
            return v + w
        x[0], x[1] = v, w
        return add()
    return plus


def _from_python(v):
    """A Python function's value for BASIC: numbers (True is -1) and
    strings; anything else is a type mismatch."""
    t = type(v)
    if t is bool:
        return -1 if v else 0
    if t is int:
        return v if -2147483648 <= v <= 2147483647 else float(v)
    if t is float or t is str:
        return v
    raise BasicError(TYPE)


# --- Closures -------------------------------------------------------------------------

def _unary(name, f, rt):
    if name == "ABS":
        def abs_():
            v = f()
            return _int32(-v) if type(v) is int and v < 0 else abs(v)
        return abs_
    if name == "INT":
        def int_():
            v = f()
            if type(v) is int:
                return v
            n = math.floor(finite(v))
            return int(n) if -2147483648 <= n <= 2147483647 else float(n)
        return int_
    if name == "SGN":
        def sgn():
            v = f()
            return 1 if v > 0 else -1 if v < 0 else 0
        return sgn
    if name == "SQR":
        def sqr():
            v = f()
            if v < 0:
                raise BasicError("-ve root")
            return math.sqrt(v)
        return sqr
    if name in ("LN", "LOG"):
        ln = name == "LN"

        def log():
            v = f()
            if v <= 0:
                raise BasicError("Log range")
            return math.log(v) if ln else math.log(v) / math.log(10)
        return log
    if name == "EXP":
        def exp():
            try:
                return finite(math.exp(f()))
            except OverflowError:
                raise BasicError("Exp range")
        return exp
    if name in ("ACS", "ASN"):
        g = math.acos if name == "ACS" else math.asin

        def arc():
            v = f()
            if v < -1 or v > 1:
                raise BasicError("-ve root")
            return g(v)
        return arc
    if name in ("SIN", "COS", "TAN", "ATN"):
        g = {"SIN": math.sin, "COS": math.cos, "TAN": math.tan, "ATN": math.atan}[name]
        return lambda: g(f())
    if name == "DEG":
        return lambda: f() * 180 / math.pi
    if name == "RAD":
        return lambda: f() * math.pi / 180
    if name == "ASC":
        def asc():
            s = f()
            return ord(s[0]) if s else -1
        return asc
    if name == "LEN":
        return lambda: len(f())
    if name == "VAL":
        return lambda: val(f())
    if name == "CHR$":
        def chr_():
            n = _int(f())
            return chr(n if 0 <= n <= 0x10FFFF else n & 0xFF)
        return chr_
    if name == "STR$":
        return lambda: num_str(f(), rt.at)
    if name == "INKEY":
        def inkey():
            k = rt.wait_key(_int(f()))
            return ord(k[0]) if k else -1
        return inkey
    if name == "INKEY$":
        return lambda: rt.wait_key(_int(f()))
    raise BasicError(SYNTAX)


def _bracketed(name, fs):
    f = fs[0]
    g = fs[1] if len(fs) > 1 else None
    h = fs[2] if len(fs) > 2 else None
    if name == "LEFT$":
        if g is None:
            return lambda: f()[:-1]  # BASIC V: all but the last character
        return lambda: f()[:_count(g())]
    if name == "RIGHT$":
        def right():
            s = f()
            n = 1 if g is None else _count(g())
            return s[len(s) - n:] if n < len(s) else s
        return right
    if name == "MID$":
        def mid():
            s = f()
            start = _int(g())
            start = start - 1 if start > 0 else 0
            return s[start:] if h is None else s[start:start + _count(h())]
        return mid
    if name == "INSTR":
        def instr():
            s, t = f(), g()
            start = _int(h()) if h is not None else 1
            start = start - 1 if start > 0 else 0
            if start > len(s):
                return 0
            return s.find(t, start) + 1
        return instr
    if name == "STRING$":
        return lambda: g() * _count(f())  # the whole string, n times
    raise BasicError(SYNTAX)


def _element(rt, key, idx):
    base = basic.base_name(key)
    suffix = key[len(base):]
    conv = basic.converter(key)
    last = idx[-1]
    outer = idx[:-1]

    def where():
        lst = rt.array(key, base, base, len(idx))
        for f in outer:
            i = _int(f())
            if i < 0 or i >= len(lst) or type(lst[i]) is not list:
                raise BasicError("Subscript")
            lst = lst[i]
        i = _int(last())
        if i < 0 or i >= len(lst):
            raise BasicError("Subscript")
        return lst, i

    def get():
        lst, i = where()
        v = lst[i]
        if type(v) is list:
            raise BasicError("Subscript")  # fewer indices than dimensions
        if suffix == "" and type(v) is float:
            return v
        return basic.from_python(suffix, v)

    def set_(v):
        lst, i = where()
        lst[i] = v if conv is None else conv(v)
    return get, set_


def _print(rt, parts):
    """PRINT: numbers right-aligned in @%'s field until a `;` (and again
    after a `,`), strings as they are; `'` a new line, `~` the next number
    in hex. A `;` or `,` at the end keeps the line."""
    out = rt.out
    newline = not parts or parts[-1][0] not in (";", ",")

    def print_():
        at = rt.at
        field = at & 0xFF
        width = out.width
        buf = []
        col = out.col
        right = True
        hex_ = False
        try:
            for kind, f in parts:
                if kind == "*":  # an FN's value: a number or a string
                    v = f()
                    kind = "s" if type(v) is str else "n"
                    f = (lambda v: lambda: v)(v)
                if kind == "n":
                    v = f()
                    s = "%X" % (_int(v) & 0xFFFFFFFF) if hex_ else num_format(v, at)
                    hex_ = False
                    if right and len(s) < field:
                        s = " " * (field - len(s)) + s
                elif kind == "s":
                    s = f()
                elif kind == ";":
                    right = False
                    continue
                elif kind == ",":
                    right = True
                    s = " " * (-col % field) if field else ""  # none at a field's start
                elif kind == "'":
                    s = "\n"
                elif kind == "~":
                    hex_ = True
                    continue
                elif kind == "SPC":
                    s = " " * _count(f[0]())
                else:  # TAB
                    x = _count(f[0]())
                    if f[1] is not None:
                        y = _count(f[1]())
                        out.write("".join(buf))
                        buf = []
                        out.write("\x1b[%d;%dH" % (y + 1, x + 1))
                        out.col = col = x % width
                        continue
                    if x < col:
                        s = "\n" + " " * x
                    else:
                        s = " " * (x - col)
                if s:
                    buf.append(s)
                    nl = s.rfind("\n")
                    col = (len(s) - nl - 1 if nl >= 0 else col + len(s)) % width
        except BaseException:
            out.write("".join(buf))
            raise
        if newline:
            buf.append("\n")
        out.write("".join(buf))
    return print_


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

class Call:
    """A PROC or FN running: what it made its own (to come back when it
    ends), the stack's height when it began, and the error handler LOCAL
    ERROR kept (MISSING until then)."""

    def __init__(self, kind, stack):
        self.kind = kind
        self.saved = []
        self.stack = stack
        self.error = MISSING


class Interpreter(basic.Runtime):
    """BBC BASIC: the program, its variables and its run, over the runtime
    both dialects share (basic.Runtime)."""

    DIALECT = "BBC"
    WHILE_END = "ENDWHILE"

    def __init__(self, write, readline=None, getkey=None, width=80, ns=None, command=None):
        super().__init__(write, readline, getkey, width, ns, command, normalize, ticks_ms())
        self.at = AT_DEFAULT
        self.time_base = 0
        self.last_rnd = 0.0
        self.err = 0
        self.erl = 0
        self.report_text = ""  # REPORT's: the last error's message
        self.calls = []  # the PROCs and FNs running: their frames (Call)
        self.handler = None  # ON ERROR's: (line, statement, depth, local)
        self.returning = False  # ENDPROC or = ran: the body's loop ends
        self.value = None  # what = gave
        self.escaped = None  # when a handler last caught ESC

    # What the runtime asks of the dialect.

    def compile_line(self, text):
        return Compiler(self, tokenize(text)).statements()

    def data_items(self, text):
        try:
            toks = tokenize(text)
        except BasicError:
            return []
        items = []
        for j, t in enumerate(toks):
            if t[0] == "W" and t[1] == "DATA" and j + 1 < len(toks) and toks[j + 1][0] == "R":
                items += split_items(toks[j + 1][1])
        return items

    def note(self, e, line):
        """An error, for ERR, ERL and REPORT: its message."""
        if e.line is not None:
            line = e.line
        message = FROM_RUNTIME.get(e.name, e.name)
        number = getattr(e, "number", None)
        self.err = number if number is not None else ERRORS.get(message, 0)
        self.erl = line if line is not None else 0
        self.report_text = message
        return message, line

    def report(self, e, line):
        message, line = self.note(e, line)
        self.out.newline_if_needed()
        self.out.write("%s%s\n" % (message, " at line %d" % line if line is not None else ""))

    def trap(self, e):
        """ON ERROR: the error noted, the handler run. A global handler
        forgets the loops, subroutines and calls that were running."""
        h = self.handler
        if h is None or not self.catchable(e):
            return False
        self.note(e, self.line())
        if not h[3]:
            self.stack.clear()
            self.restore_calls(0)
        self.li, self.si = h[0], h[1]
        return True

    def execute(self, text):
        self.handler = None  # a program's handler ends with it
        self.restore_calls(0)
        basic.Runtime.execute(self, text)

    def do_run(self, line):
        self.handler = None
        self.restore_calls(0)
        return basic.Runtime.do_run(self, line)

    def report_break(self, line, how):
        self.err = 17 if how == "BREAK" else 0
        self.erl = line if line is not None else 0
        word = "Escape" if how == "BREAK" else "STOP"
        self.out.write("%s%s\n" % (word, " at line %d" % line if line is not None else ""))

    def line_refs(self, text):
        return []  # RENUMBER comes with step 3e

    def renumber_line(self, text, new):
        return text

    def num_text(self, x):
        return num_str(x)

    # Variables: names are case-sensitive, so Python's must match exactly;
    # a variable never written is a mistake, but A% to Z% always exist.

    def resolve(self, base, spelling):
        if base in self.unresolved:
            return None
        if spelling not in self.ns:
            self.unresolved.add(base)
            return None
        self.names[base] = spelling
        return spelling

    def unset(self, key):
        if _is_static(key):
            return 0
        raise BasicError("No such variable")

    def undimmed(self, key, spelling, n):
        raise BasicError("Array")

    def clear(self):
        kept = [(k, v) for k, v in self.vars.items() if _is_static(k)]
        super().clear()
        for k, v in kept:
            self.vars[k] = v

    def get_at(self):
        return self.at

    def set_at(self, v):
        self.at = _int(v)

    def do_list(self, low, high):
        for number in self.order:
            if low <= number <= high:
                self.out.write("%5d %s\n" % (number, self.program.lines[number]))

    def eval(self, text):
        """EVAL: a numeric expression in a string, compiled when it runs."""
        c = Compiler(self, tokenize(text))
        f = c.num()
        if c.peek() is not None:
            raise BasicError(SYNTAX)
        return f()

    # Input.

    def do_input(self, items, whole):
        """INPUT's prompts and variables: a prompt's text before the next
        line read ("?" when none, or after a prompt followed by , or ;), a
        line's items, split at commas, filling variables until it runs
        out. INPUT LINE takes the whole line."""
        if self.readline is None:
            raise BasicError(MISTAKE)
        prompt = None
        queue = []
        for kind, x in items:
            if kind == "p":
                if prompt is not None:
                    self.out.write(prompt)
                prompt = x
                queue = []
                continue
            if not queue:
                self.out.write("?" if prompt is None else prompt)
                prompt = None
                line = self.readline()
                self.out.col = 0
                queue = [(line, False)] if whole else split_items(line)
            text, quoted = queue.pop(0)
            _, set_, vkind = x
            if vkind == "s":
                set_(text if whole or quoted else text.lstrip())
            else:
                set_(val(text))
        if prompt is not None:
            self.out.write(prompt)
        return None

    # PROC and FN.

    def find_def(self, kind, name):
        """Where DEF PROCname or DEF FNname is (the first statement of its
        line): (line, its header), or None."""
        defs = self.wends.get("DEFS")
        if defs is None:
            defs = {}
            for li, number in enumerate(self.order):
                if not self.program.lines[number].startswith("DEF"):
                    continue
                try:
                    k = self.compiled(li)[1][0]
                except BasicError as e:
                    if e.line is None:
                        e.line = number
                    raise
                if type(k) is tuple and k[0] == "DEF":
                    defs.setdefault((k[1], k[2]), (li, k))
            self.wends["DEFS"] = defs
        return defs.get((kind, name))

    def call(self, kind, name, args):
        """PROCname(args) or FNname(args): the program's, else Python's."""
        found = self.find_def(kind, name)
        if found is None:
            return self.call_python(kind, name, args)
        li, header = found
        params = header[3]
        if len(params) != len(args):
            raise BasicError("Arguments")
        values = []
        for (key, ret), (f, k, _) in zip(params, args):
            v = _want(f, k, "s" if key.endswith("$") else "n")()
            values.append(v)
        place = (self.li, self.si)
        frame = Call(kind, len(self.stack))
        self.calls.append(frame)
        depth = len(self.calls)
        try:
            for (key, ret), v in zip(params, values):
                self.make_local(frame, key)
                self.scalar(key, basic.base_name(key))[1](v)
            if header[4] is not None:  # DEF FNname(x) = expression
                result = header[4]()
            else:
                self.run_body(li, 1, depth)
                result = self.value
            # RETURN parameters: their values back into the variables passed.
            back = [(self.scalar(key, basic.base_name(key))[0](), a[2])
                    for (key, ret), a in zip(params, args) if ret]
        except BaseException:
            self.no_cont = True  # CONT cannot go back into a call
            raise
        finally:
            self.restore_calls(depth - 1)
        for v, set_ in back:
            if set_ is None:
                raise BasicError("Arguments")  # RETURN needs a variable
            set_(v)
        self.li, self.si = place
        return result

    def call_python(self, kind, name, args):
        """A PROC or FN the program does not define: the Python function of
        that name, with the arguments' values (spec §9.1)."""
        f = self.ns.get(name)
        if f is None or not callable(f):
            raise BasicError("No such FN/PROC")
        values = [g() for g, _, _ in args]
        try:
            v = f(*values)
        except (BasicError, KeyboardInterrupt):
            raise
        except Exception as e:  # the function's own: its type and message
            raise BasicError("%s: %s" % (type(e).__name__, e))
        finally:
            self.unresolved.clear()  # it may have made names
        return _from_python(v) if kind == "FN" else None

    def run_body(self, li, si, depth):
        """A PROC's or FN's statements, from `si` on line `li`, until ENDPROC
        or `=`; a LOCAL ERROR handler of this call's catches errors."""
        self.li, self.si = li, si
        while True:
            try:
                self.body_steps()
                return
            except KeyboardInterrupt:
                e = BasicError("ESCAPE")
                h = self.handler
                if h is None or not h[3] or h[2] != depth or not self.catchable(e):
                    raise
            except basic.Halted:
                raise
            except Exception as exc:
                e = basic.as_error(exc)
                h = self.handler
                if e is None or h is None or not h[3] or h[2] != depth:
                    raise
            self.note(e, self.line())
            self.li, self.si = self.handler[0], self.handler[1]

    def body_steps(self):
        li, i = self.li, self.si
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
                if self.returning:
                    self.returning = False
                    return
                if self.halted is not None:
                    raise basic.Halted
                li, i = self.li, self.si
                stmts = self.compiled(li)[0]
                if i < len(stmts):
                    continue
            if li < 0 or li + 1 >= len(self.order):
                self.halted = "END"  # the program's end, before ENDPROC
                raise basic.Halted
            li += 1
            self.li = li
            self.si = i = 0
            stmts = self.compiled(li)[0]

    def end_proc(self):
        if not self.calls or self.calls[-1].kind != "PROC":
            raise BasicError("Not in a procedure")
        self.returning = True
        return True

    def fn_return(self, v):
        if not self.calls or self.calls[-1].kind != "FN":
            raise BasicError("Not in a function")
        self.value = v
        self.returning = True
        return True

    def make_local(self, frame, key):
        """`key` the call's own: its value now kept, to come back when the
        call ends (in BASIC and in Python's namespace)."""
        base = basic.base_name(key)
        py = self.names.get(base, base)
        frame.saved.append((key, py, self.vars.get(key, MISSING), self.ns.get(py, MISSING),
                            self.exports.get(py, MISSING)))

    def do_local(self, keys):
        if not self.calls:
            raise BasicError("Not LOCAL")
        frame = self.calls[-1]
        for key in keys:
            self.make_local(frame, key)
            self.scalar(key, basic.base_name(key))[1]("" if key.endswith("$") else 0)

    def restore_calls(self, depth):
        """The calls deeper than `depth` ended: their variables and handlers
        back, their loops and subroutines gone."""
        while len(self.calls) > depth:
            frame = self.calls.pop()
            for key, py, v, n, x in reversed(frame.saved):
                for d, old in ((self.vars, v), (self.ns, n), (self.exports, x)):
                    k = key if d is self.vars else py
                    if old is MISSING:
                        d.pop(k, None)
                    else:
                        d[k] = old
            if frame.error is not MISSING:
                self.handler = frame.error
            del self.stack[frame.stack:]
        self.returning = False

    # ON ERROR.

    def catchable(self, e):
        """Whether a handler may catch `e`: anything but ESC pressed again
        within a second of an ESC it caught, which stops the program (so
        one that catches Escape can still be stopped)."""
        if e.name != "ESCAPE":
            return True
        now = ticks_ms()
        if self.escaped is not None and now - self.escaped < 1000:
            self.escaped = None
            return False
        self.escaped = now
        return True

    def set_handler(self, start, local):
        self.handler = (self.li, start, len(self.calls), local)

    def error_off(self):
        self.handler = None

    def local_error(self):
        """LOCAL ERROR: the handler in use comes back when the call ends."""
        if not self.calls:
            raise BasicError("Not LOCAL")
        frame = self.calls[-1]
        if frame.error is MISSING:
            frame.error = self.handler

    def restore_error(self):
        """RESTORE ERROR: the handler LOCAL ERROR kept, back now."""
        if self.calls and self.calls[-1].error is not MISSING:
            self.handler = self.calls[-1].error
            self.calls[-1].error = MISSING

    # Constructs over several lines.

    def scan(self, li, si, block, stops):
        """From after statement `si` of line `li`: the first statement, in
        this construct (`block`: its opening and ending keywords), that
        ends it or whose keyword is in `stops`, skipping the constructs of
        its kind inside it. (line, statement, keyword, what the keyword
        holds), or None at the program's end."""
        opens, closes = block
        depth = 0
        si += 1
        while True:
            try:
                kinds = self.compiled(li)[1]
            except BasicError as e:
                if e.line is None and li >= 0:
                    e.line = self.order[li]  # the mistake is that line's
                raise
            while si < len(kinds):
                k = kinds[si]
                name = k[0] if type(k) is tuple else k
                if name in opens:
                    depth += 1
                elif name in closes:
                    if depth == 0:
                        return li, si, name, k
                    depth -= 1
                elif depth == 0 and name in stops:
                    return li, si, name, k
                si += 1
            if li < 0 or li + 1 >= len(self.order):
                return None
            li += 1
            si = 0

    def block_end(self, block, stops, missing):
        """The place after where this construct goes on (`stops`) or ends,
        from the statement running; remembered until the program changes."""
        place = (block, stops, self.li, self.si)
        found = self.wends.get(place) if self.li >= 0 else None
        if found is None:
            r = self.scan(self.li, self.si, block, stops)
            if r is None:
                raise BasicError(missing)
            found = (r[0], r[1] + 1)
            if self.li >= 0:
                self.wends[place] = found
        return found

    def do_case(self, v, kind):
        """CASE: on after the first WHEN with a value equal to `v`, else
        after OTHERWISE, else after ENDCASE."""
        place = ("CASE", self.li, self.si)
        clauses = self.wends.get(place) if self.li >= 0 else None
        if clauses is None:
            clauses = []
            li, si = self.li, self.si
            while True:
                r = self.scan(li, si, BLOCK_CASE, ("WHEN", "OTHERWISE"))
                if r is None:
                    raise BasicError("Missing ENDCASE")
                clauses.append(r)
                li, si = r[0], r[1]
                if r[2] == "ENDCASE":
                    break
            if self.li >= 0:
                self.wends[place] = clauses
        for li, si, name, k in clauses:
            if name == "WHEN":
                for f, wkind in k[1]:
                    if wkind != kind and "*" not in (wkind, kind):
                        raise BasicError(TYPE)
                    w = f()
                    _same_kind(v, w)
                    if w == v:
                        break
                else:
                    continue
            self.li, self.si = li, si + 1
            return True
        return None  # not reached: the last clause is ENDCASE's

    def case_end(self):
        """WHEN or OTHERWISE reached at the end of a clause: on after the
        ENDCASE."""
        self.li, self.si = self.block_end(BLOCK_CASE, (), "Missing ENDCASE")
        return True

    # REPEAT frames share the stack with FOR, GOSUB and WHILE: ("R", line,
    # statement).

    def push_repeat(self):
        self.stack.append(("R", self.li, self.si + 1))

    def do_until(self, cond):
        stack = self.stack
        j = len(stack) - 1
        while j >= 0 and stack[j][0] != "R":
            if stack[j][0] == "G":
                j = -1
                break
            j -= 1
        if j < 0:
            raise BasicError("Not in a REPEAT loop")
        del stack[j + 1:]
        if cond() != 0:
            stack.pop()
            return None
        self.li, self.si = stack[j][1], stack[j][2]
        return True

    def wait_key(self, cs):
        """A key: waiting for one (GET), or up to `cs` centiseconds (INKEY);
        "" when none came."""
        end = None if cs is None else ticks_ms() + 10 * cs
        while True:
            k = self.key()
            if k:
                return k
            if sleep_ms is None or (end is not None and ticks_ms() >= end):
                return ""
            sleep_ms(10)

    # Time and chance.

    def time(self):
        return _int32(ticks_ms() // 10 - self.time_base)

    def set_time(self, v):
        self.time_base = ticks_ms() // 10 - _int(v)

    def rnd(self, x):
        """RND: a 32-bit integer; RND(1) a real below 1, RND(n) a whole
        number from 1 to n, RND(0) the last real again, RND(-n) seeds."""
        if x is None:
            return _int32(int(self.random.next() * 4294967296.0))
        if x < 0:
            self.random.seed(_int(x))
            return _int(x)
        if x == 0:
            return self.last_rnd
        if x == 1:
            self.last_rnd = self.random.next()
            return self.last_rnd
        return int(self.random.next() * _int(x)) + 1
