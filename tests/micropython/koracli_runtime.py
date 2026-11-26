# SPDX-License-Identifier: GPL-3.0-or-later
#
# The runtime KoraCLI's BASIC dialects share (user/koracli/basic.py,
# `basic.Runtime`) on the unix port, under a toy dialect of a few words:
# what a dialect gives it (compiling a line, DATA, its error and break
# messages, RENUM's references, the header's name) and what it does with
# them, whatever the dialect: the run, GOTO, GOSUB, FOR, WHILE, DATA, CONT,
# SAVE, LOAD and RENUM. Classic BASIC's own tests are koracli_basic.py and
# koracli_files.py.

import os

import basic
from basic import BasicError

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


def words(part):
    """A statement's word and its argument (this MicroPython has no
    str.partition)."""
    w = part.strip().split(" ", 1)
    return w[0], (w[1].strip() if len(w) > 1 else "")


class Toy(basic.Runtime):
    """Statements are a word and its argument, one per `;`: SAY text,
    GO line, SUB line, BACK, FOR name limit, NEXT, WHILE name, LOOP, SET
    name value, ADD name value, READ name, DATA a,b, STOP, BREAK, END.
    Lines are named by GO and SUB."""

    DIALECT = "Toy"
    WHILE_END = "LOOP"

    def compile_line(self, text):
        stmts = []
        kinds = []
        for part in text.split(";"):
            word, arg = words(part)
            stmts.append(self.statement(word, arg))
            kinds.append(word)
        return stmts, kinds

    def statement(self, word, arg):
        out = self.out
        if word == "SAY":
            return lambda: out.write(arg + "\n")
        if word == "GO":
            return lambda: self.goto(int(arg))
        if word == "SUB":
            def sub():
                self.push_gosub()
                return self.goto(int(arg))
            return sub
        if word == "BACK":
            return self.do_return
        if word == "FOR":
            name, limit = arg.split()

            def for_():
                self.scalar_set(name, 1.0)
                self.push_for(name, float(limit), 1.0)
            return for_
        if word == "NEXT":
            return lambda: self.do_next(None)
        if word == "WHILE":
            return lambda: self.do_while(lambda: self.scalar_get(arg))
        if word == "LOOP":
            return self.do_wend
        if word in ("SET", "ADD"):
            name, value = arg.split()

            def set_():
                v = float(value) + (self.scalar_get(name) if word == "ADD" else 0.0)
                self.scalar_set(name, v)
            return set_
        if word == "SHOW":
            get = self.scalar(arg, arg)[0]
            return lambda: out.write("%s = %r\n" % (arg, get()))
        if word == "READ":
            return lambda: self.scalar_set(arg, float(self.next_data()[0]))
        if word == "DATA":
            return lambda: None
        if word == "STOP":
            return lambda: self.halt("STOP")
        if word == "BREAK":
            def break_():
                raise KeyboardInterrupt
            return break_
        if word == "END":
            return lambda: self.halt("END")
        if word == "CONT":
            return self.do_cont
        if word == "RUN":
            return lambda: self.do_run(None)
        if word == "SAVE":
            return lambda: self.do_save(arg)
        if word == "LOAD":
            return lambda: self.do_load(arg)
        if word == "RENUM":
            return lambda: self.do_renum(100, 10)
        if word == "LIST":
            return lambda: self.do_list(0, basic.MAX_LINE)
        raise BasicError(basic.SYNTAX)

    def data_items(self, text):
        items = []
        for part in text.split(";"):
            word, arg = words(part)
            if word == "DATA":
                items += [(a.strip(), False) for a in arg.split(",")]
        return items

    def report(self, e, line):
        self.out.newline_if_needed()
        self.out.write("toy: %s%s\n" % (e.name.lower(), " at %d" % line if line is not None else ""))

    def report_break(self, line, how):
        self.out.write("toy: %s%s\n" % (how.lower(), " at %d" % line if line is not None else ""))

    def line_refs(self, text):
        refs = []
        pos = 0
        for part in text.split(";"):
            p = part.strip()
            word, arg = words(p)
            start = text.index(p, pos) + len(word) + 1
            pos = start
            if word in ("GO", "SUB"):
                refs.append((start, start + len(arg), int(arg)))
        return refs

    def renumber_line(self, text, new):
        for s, e, n in reversed(self.line_refs(text)):
            if n in new:
                text = text[:s] + str(new[n]) + text[e:]
        return text

    def warn_ref(self, target, line):
        self.out.write("toy: no line %d (in %d)\n" % (target, line))

    def num_text(self, x):
        return str(x)


ns = {}
t = Toy(lambda s: print(s, end=""), ns=ns)


def program(*lines):
    t.do_new()  # the program and the variables gone
    for line in lines:
        t.enter(line)


def direct(text):
    print("> " + text)
    t.execute(text)


# The run: lines in order, GOTO, GOSUB and RETURN, FOR and NEXT, WHILE.
print("== running")
program("10 SAY one; SUB 100; GO 30", "20 SAY not here", "30 FOR I 3; SAY loop; NEXT",
        "40 SET W 1; WHILE W; SAY while; SET W 0; LOOP; SAY after; END", "100 SAY sub; BACK")
direct("RUN")
check(ns.get("I") == 4.0, "FOR's variable, shared with Python: %r" % ns.get("I"))

# DATA, read in line order.
program("10 READ A; READ B", "20 DATA 5, 6", "30 DATA 7")
direct("RUN")
check(ns["A"] == 5.0 and ns["B"] == 6.0, "DATA in order")

# Errors and breaks in the dialect's words; CONT after STOP or a break.
print("== errors and breaks")
program("10 SAY a; STOP; SAY b", "20 BREAK; SAY c")
direct("RUN")
direct("CONT")
direct("CONT")
program("10 GO 99")
direct("RUN")
direct("NOSUCH")
direct("BREAK")
direct("CONT")

# Python's values, read back: numbers convert; a write is Python's too.
program()
ns["X"] = 7
direct("SHOW X")
direct("SET X 2; SHOW X")
check(ns["X"] == 2.0, "the write, in Python's namespace: %r" % ns["X"])
ns["X"] = "seven"
direct("SHOW X")

# SAVE, LOAD and RENUM, through the dialect's hooks.
print("== files and RENUM")
DIR = "/tmp/koracli-runtime-test"
try:
    os.mkdir(DIR)
except OSError:
    pass
os.chdir(DIR)
program("10 SAY hi; GO 30", "30 SUB 50; END", "50 SAY sub; BACK; GO 77")
direct("SAVE PROG")
with open("PROG.BAS") as f:
    text = f.read()
check(text == "0 REM !KoraBasic:Toy\n10 SAY hi; GO 30\n30 SUB 50; END\n"
      "50 SAY sub; BACK; GO 77\n", "the file, with the toy's header: %r" % text)
direct("RENUM")
direct("LIST")
with open("CLASSIC.BAS", "w") as f:
    f.write("0 REM !KoraBasic:Classic\n10 PRINT 1\n")
direct("LOAD CLASSIC")
direct("LOAD PROG")
direct("LIST")
os.chdir("/")
for name in os.listdir(DIR):
    os.remove(DIR + "/" + name)
os.rmdir(DIR)

print("koracli runtime: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
