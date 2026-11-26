# SPDX-License-Identifier: GPL-3.0-or-later
#
# The variables Python and Classic BASIC share (project_doc/koracli-spec.md
# §7, user/koracli/classic.py) on the unix port: BASIC's writes in Python's
# namespace under the spelling first written, Python's names read exactly
# then without regard to case, collisions, Python's changes seen at the next
# read, the type rules, arrays as lists, RUN clearing only BASIC's, FN's
# parameters, and SYS calling Python.

import classic

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


ns = {}
commands = []


def command(name, args):
    """SYS's OS commands: only `echo` is there."""
    if name != "echo":
        return False
    commands.append((name, args))
    return True


b = classic.Interpreter(lambda s: print(s, end=""), ns=ns, command=command)


def program(*lines):
    b.execute("NEW")
    for line in lines:
        b.enter(line)


def direct(text):
    print("> " + text)
    b.execute(text)


# The spec's example: Python's msg read as msg$.
ns["msg"] = "hello world"
program("10 PRINT msg$")
direct("RUN")

# BASIC's writes, under the spelling first written, with Python's types.
direct('A = 5: B$ = "X": C% = 7.9: Word$ = "a": WORD$ = "b"')
check(type(ns["A"]) is float and ns["A"] == 5 and ns["B"] == "X" and type(ns["C"]) is int and
      ns["C"] == 7, "numbers as float, %% as int, $ as str: %r" % ns)
check(ns["Word"] == "b" and "WORD" not in ns, "the spelling first written")
ns["Total"] = 1
direct("TOTAL = 9: total = total + 1")
check(ns["Total"] == 10 and "TOTAL" not in ns and "total" not in ns,
      "a name Python has is the one BASIC writes: %r" % ns.get("Total"))

# Reading Python's names: the same spelling first, then without regard to case.
ns["Count"] = 3
ns["abc"] = 1
ns["ABC"] = 2
direct("PRINT COUNT; abc; ABC")  # abc and ABC: one BASIC variable, Python's abc
direct("PRINT ABC")  # looked for again in the next command: Python's ABC
direct("PRINT Abc")

# A, A$, A% and A() stay separate in BASIC; Python sees the last written.
direct('A = 5: A$ = "X": A% = 2: PRINT A; A$; A%')
check(ns["A"] == 2 and type(ns["A"]) is int, "Python sees the last written: %r" % ns["A"])

# Python's changes are seen at BASIC's next read, of any of them.
ns["A"] = 3
direct("PRINT A; A%")
ns["A"] = "now a string"
direct("PRINT A$")
direct("PRINT A")
direct("A = 1: PRINT A; A$")  # BASIC's write is the newest again

# The type rules: numbers convert, True is -1, nothing else converts.
ns["n"] = 2
ns["k"] = -1.5
ns["flag"] = True
ns["d"] = {}
direct('PRINT N / 4; K%; FLAG: IF FLAG THEN PRINT "TRUE IS"; FLAG')
direct("PRINT D")
ns["big"] = 3000000000
direct("PRINT BIG; BIG%")

# Arrays are Python lists, shared as they are.
direct("DIM V(3): V(2) = 7")
check(ns["V"] == [0.0, 0.0, 7.0, 0.0], "a DIM is a list: %r" % ns["V"])
ns["V"][1] = 4
direct("PRINT V(1); V(2)")
ns["L"] = [1, 2, 3]
direct("PRINT L(2): L(0) = 9")
check(ns["L"] == [9.0, 2, 3], "BASIC writes into Python's list: %r" % ns["L"])
direct("PRINT L(3)")
direct("DIM M(1, 2): M(1, 2) = 5")
check(ns["M"] == [[0.0, 0.0, 0.0], [0.0, 0.0, 5.0]], "two dimensions, nested: %r" % ns["M"])
ns["names"] = ["ada", "bob"]
direct("PRINT NAMES$(1); LEN(NAMES$(0))")
direct("PRINT NAMES(0)")
ns["grid"] = [[1, 2], [3, 4]]
direct("PRINT GRID(1, 0); GRID(0)")
direct("PRINT MSG(1)")

# FOR's variable, in Python after the loop (one past the limit, as on a C64).
direct("FOR I = 1 TO 3: NEXT")
check(ns["I"] == 4, "FOR's variable: %r" % ns.get("I"))

# A function's parameters are its own, whatever Python has.
ns["x"] = 100
direct("DEF FNTWICE(X) = X * 2: PRINT FNTWICE(3); X; FNTWICE(X)")

# RUN, CLR and NEW take away only what BASIC still holds.
program("10 OWN = 1: CHANGED = 2", "20 END")
direct("RUN")
check(ns.get("OWN") == 1 and ns.get("CHANGED") == 2, "the program's variables")
ns["CHANGED"] = "Python's now"
ns["python_only"] = 7
direct("CLR")
check("OWN" not in ns and ns["CHANGED"] == "Python's now" and ns["python_only"] == 7 and
      ns["msg"] == "hello world" and ns["Total"] == 10,
      "CLR takes BASIC's away, Python's stay: %r" % sorted(ns))
direct("PRINT OWN")

# SYS calls a Python function, else an OS command or wrapper.
calls = []
ns["greet"] = lambda *args: calls.append(args)
direct('SYS "greet", 1, "two", 2 + 1.5')
check(calls == [(1.0, "two", 3.5)], "a Python function: %r" % calls)
direct('SYS "echo", 5, "x"')
check(commands == [("echo", ["5", "x"])], "an OS command, numbers as BASIC prints them: %r"
      % commands)
direct('SYS "nothing"')


def boom(what):
    raise ValueError("no " + what)


ns["boom"] = boom
program('10 SYS "boom", "luck"')
direct("RUN")
ns["setter"] = lambda: ns.__setitem__("made", 42)
direct('SYS "setter": PRINT MADE')
direct("SYS 49152")

print("koracli shared: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
