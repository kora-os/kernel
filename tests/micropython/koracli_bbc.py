# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's BBC BASIC (user/koracli/bbc.py, step 3b) on the unix port: the
# program store (abbreviations written in full, keywords in capitals only),
# numbers as a BBC prints them (@%, integers and reals), type-in listings
# whose output (koracli_bbc.py.exp) is what BBC BASIC V would print, BBC's
# error messages and ERR, Escape and STOP, INPUT, GET and INKEY, and the
# variables shared with Python, case-sensitive.

import basic
import bbc

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


class Host:
    """What KoraCLI gives the interpreter: the output, typed lines, keys."""

    def __init__(self):
        self.lines = []
        self.keys = []

    def write(self, s):
        print(s, end="")

    def readline(self):
        line = self.lines.pop(0) if self.lines else ""
        print(line)  # as the line editor echoes it
        return line

    def getkey(self):
        k = self.keys.pop(0) if self.keys else ""
        if k is KeyboardInterrupt:
            raise KeyboardInterrupt  # ESC, while the program waits for a key
        return k


host = Host()
ns = {}
b = bbc.Interpreter(host.write, host.readline, host.getkey, 40, ns)


def program(*lines):
    b.execute("NEW")
    for line in lines:
        b.enter(line)


def run(title, *lines):
    print("== " + title)
    program(*lines)
    b.execute("RUN")


def direct(text):
    print(">" + text)
    b.execute(text)


# The program store: abbreviations in full, the rest as typed.
p = basic.Program(bbc.normalize)
p.enter('10 P."HELLO";:G.20')
check(p.lines[10] == 'PRINT"HELLO";:GOTO20', "P. and G.: %r" % p.lines[10])
p.enter('20 GOS.100:PRI."P. stays in a string":REM P. here too')
check(p.lines[20] == 'GOSUB100:PRINT"P. stays in a string":REM P. here too',
      "GOS., PRI., strings and REM: %r" % p.lines[20])
p.enter("30 F.I=1TO3:N.:x=1.5")
check(p.lines[30] == "FORI=1TO3:NEXT:x=1.5", "F., N., a number's point: %r" % p.lines[30])
p.enter("40 print = 1")
check(p.lines[40] == "print = 1", "small letters are names: %r" % p.lines[40])

# Numbers as PRINT shows them, in @%'s default (&90A) and others.
for x, s in ((0, "0"), (3, "3"), (-7, "-7"), (2147483647, "2147483647"), (0.5, "0.5"),
             (1 / 3, "0.333333333"), (0.1, "0.1"), (0.05, "5E-2"), (1e9, "1E9"),
             (123456789.0, "123456789"), (1234567890.0, "1.23456789E9"),
             (2.0, "2"), (-1.5e-10, "-1.5E-10"), (1e10, "1E10")):
    check(bbc.num_format(x) == s, "%r prints as %r, not %r" % (x, s, bbc.num_format(x)))
check(bbc.num_format(3.14159, 0x20209) == "3.14", "F format")
check(bbc.num_format(1234.5, 0x10409) == "1.235E3", "E format: %r" % bbc.num_format(1234.5, 0x10409))
check(bbc.num_str(1 / 3, 0x20209) == "0.333333333" and bbc.num_str(2.5, 0x1020209) == "2.50",
      "STR$: nine digits unless bit 24 of @% is set")

# Type-in listings.
run("PRINT: fields, ; and ' and ~",
    "10 PRINT 1",
    "20 PRINT 1,2",
    '30 PRINT "A",1',
    '40 PRINT "X";1;"Y"',
    "50 PRINT ;12;34",
    "60 PRINT ~255, ~-1",
    "70 PRINT \"one\"'\"two\"",
    '80 PRINT "KEPT";',
    '90 PRINT " LINE"',
    '100 PRINT TAB(5);"T";SPC(3);"S"',
    "110 @%=&20208: PRINT PI: @%=&90A")

run("arithmetic: integers and reals",
    "10 PRINT 7 DIV 2, 7 MOD 2, -7 DIV 2, -7 MOD 2",
    "20 PRINT 7/2, 2^10, 10/5",
    "30 PRINT 2147483647+1",
    "40 PRINT 100000*100000",
    "50 PRINT &FF, %1010, &FFFFFFFF",
    "60 PRINT 1<<4, -16>>2, -16>>>28",
    "70 PRINT 6 AND 3, 6 OR 3, 6 EOR 3, NOT 0",
    "80 PRINT 1=1, 1=2, \"a\"<\"b\"",
    "90 PRINT -2^2, NOT 1=1",
    "100 A%=7.9: B%=-7.9: PRINT A%, B%",
    "110 X=3: X+=4: X-=1: PRINT X",
    '120 S$="ab": S$+="cd": PRINT S$')

run("functions",
    "10 PRINT SQR 16, ABS -3, SGN -2, INT -2.5, INT 2.5",
    "20 PRINT LEN \"hello\", ASC \"A\", ASC \"\", CHR$65",
    '30 PRINT LEFT$("hello",2), RIGHT$("hello",3), MID$("hello",2,3), MID$("hello",3)',
    '40 PRINT LEFT$("hello"), RIGHT$("hello"), STRING$(3,"ab")',
    '50 PRINT INSTR("hello","l"), INSTR("hello","l",4), INSTR("hello","z")',
    '60 PRINT VAL "12.5abc", VAL "x", STR$ 42, STR$~255, STR$(1/3)',
    "70 PRINT LN 1, LOG 100, EXP 0, DEG PI, RAD 180",
    "80 PRINT SIN 0, COS 0, ATN 1*4",
    '90 PRINT EVAL("6*7"), TRUE, FALSE')

run("control: GOTO, GOSUB, ON, FOR, IF THEN ELSE",
    "10 FOR I%=1 TO 3: PRINT I%;: NEXT: PRINT",
    "20 FOR X=10 TO 1 STEP -4: PRINT X;: NEXT X: PRINT",
    '30 FOR A=1 TO 2: FOR B=1 TO 2: PRINT A;B;" ";: NEXT B,A: PRINT',
    "40 GOSUB 200",
    "50 N=2: ON N GOTO 60, 70",
    '60 PRINT "not here"',
    '70 ON 5 GOSUB 200 ELSE PRINT "ON ELSE"',
    '80 IF 1 THEN PRINT "then" ELSE PRINT "else"',
    '90 IF 0 THEN PRINT "then" ELSE PRINT "else"',
    '100 IF 2>1 PRINT "no THEN": IF 0 PRINT "skipped"',
    "110 IF 1 THEN 130",
    '120 PRINT "not here"',
    '130 GOTO 100+40',
    '140 PRINT "computed GOTO": END',
    '200 PRINT "in a subroutine": RETURN')

run("DATA, READ and RESTORE",
    '10 READ A, B$, C',
    '20 PRINT A; B$; C',
    '30 RESTORE 90: READ D$: PRINT D$',
    '80 DATA 1, "two, with a comma", 3',
    "90 DATA last item: colons are data")

run("arrays and SWAP",
    "10 DIM A(3), M$(1,1)",
    '20 A(2)=5: M$(1,0)="m": PRINT A(2); A(0); M$(1,0)',
    "30 X=1: Y=2: SWAP X,Y: PRINT X;Y")

# Variables: a variable never written is a mistake, A% to Z% always exist
# (and stay through RUN and CLEAR), names are case-sensitive.
print("== variables")
direct("PRINT A%")
direct("PRINT nope")
direct("Z%=9: CLEAR: PRINT Z%")
direct("count=1: Count=2: PRINT count; Count")
direct("DIM q(2): PRINT q(2)")
direct("PRINT r(1)")

# Errors, in BBC's words, with ERR.
print("== errors")
program("10 X=1", "20 PRINT Y")
direct("RUN")
check(b.err == 26 and b.erl == 20, "No such variable is 26, at 20: %d %d" % (b.err, b.erl))
direct("FRED")
check(b.err == 4, "Mistake is 4: %d" % b.err)
direct('PRINT "A"+1')
direct("PRINT 1/0")
direct("PRINT SQR -1")
direct("PRINT LN 0")
direct("GOTO 999")
direct("RETURN")
direct("NEXT")
direct("FOR I=1 3")
direct("PRINT (1")
direct('PRINT "open')
direct("A%=3E10")
direct("DIM A(2): DIM A(3)")
direct("PRINT A(5)")
direct("ON 3 GOTO 10")
direct("READ X: READ X: READ X")
direct("CONT")

# STOP and Escape, with CONT going on.
print("== STOP and Escape")
program('10 PRINT "before"', "20 STOP", '30 PRINT "after"', "40 K=GET", '50 PRINT "got"; K')
direct("RUN")
check(b.err == 0 and b.erl == 20, "STOP at 20")
host.keys = [KeyboardInterrupt]
direct("CONT")
check(b.err == 17, "Escape is 17: %d" % b.err)
host.keys = ["A"]
direct("CONT")

# INPUT and the keys.
print("== INPUT and keys")
program('10 INPUT "Name", N$', '20 INPUT "Age" A, B', "30 INPUT LINE L$",
        '40 PRINT N$; " "; A; " "; B; " ["; L$; "]"')
host.lines = ["  Ada", "36, 7", "  keep  spaces, and commas"]
direct("RUN")
program("10 INPUT X, Y", '20 PRINT X + Y')
host.lines = ["1", "2"]
direct("RUN")
host.keys = ["", "z", "q"]
direct("PRINT INKEY(0); INKEY$(0); GET$")

# Python's variables, case-sensitive.
print("== Python's variables")
ns["Msg"] = "hello"
ns["n"] = 3
ns["msg"] = 1.5
direct("PRINT Msg$; n%; msg")
direct("total = n% * 2: name$ = Msg$ + \"!\"")
check(ns["total"] == 6.0 and type(ns["total"]) is float, "a real is Python's float")
check(ns["name"] == "hello!", "a string, under its own name")
direct("i% = 7")
check(ns["i"] == 7 and type(ns["i"]) is int, "a % variable is Python's int")
direct("PRINT MSG$")

# Constructs over several lines (step 3c): IF THEN at a line's end, ELSE
# and ENDIF; CASE, WHEN, OTHERWISE, ENDCASE; REPEAT and UNTIL; WHILE and
# ENDWHILE; nested, and each one's mistakes.
run("IF over several lines",
    "10 FOR I%=1 TO 3",
    "20 IF I%=2 THEN",
    '30   PRINT "two";',
    "40   IF TRUE THEN",
    '50     PRINT " (nested)";',
    "60   ENDIF",
    "70 ELSE",
    '80   PRINT "not two";',
    "90   IF FALSE THEN",
    '100    PRINT "never"',
    "110  ENDIF",
    "120 ENDIF",
    '130 PRINT " after"',
    "140 NEXT",
    '150 IF 1 THEN PRINT "one line" ELSE PRINT "other"')

run("CASE",
    '10 FOR N%=0 TO 4',
    "20 CASE N% OF",
    '30 WHEN 1, 2: PRINT N%; " small"',
    '40 WHEN 3',
    '50   CASE "x" OF',
    '60   WHEN "x": PRINT "3, inner x"',
    "70   ENDCASE",
    '80 OTHERWISE PRINT N%; " other"',
    "90 ENDCASE",
    "100 NEXT",
    '110 CASE "b" OF',
    '120 WHEN "a": PRINT "a"',
    '130 ENDCASE: PRINT "no clause: after ENDCASE"')

run("REPEAT and WHILE",
    "10 I%=0: REPEAT I%+=1: PRINT I%;: UNTIL I%=3: PRINT",
    "20 REPEAT",
    "30   J%=0",
    "40   REPEAT J%+=1: UNTIL J%=2",
    "50   I%-=1",
    "60 UNTIL I%=0",
    '70 PRINT "I% "; I%; ", J% "; J%',
    "80 K=3",
    "90 WHILE K>0",
    "100  PRINT K;",
    "110  WHILE FALSE: PRINT \"never\": ENDWHILE",
    "120  K-=1",
    "130 ENDWHILE",
    '140 PRINT: WHILE FALSE',
    '150 PRINT "skipped"',
    '160 ENDWHILE: PRINT "WHILE FALSE skips its body"')

print("== the constructs' mistakes")
program("10 IF FALSE THEN", '20 PRINT "x"')
direct("RUN")
check(b.err == 49, "Missing ENDIF is 49: %d" % b.err)
program("10 CASE 1 OF", "20 WHEN 2: PRINT")
direct("RUN")
program("10 CASE 1", "20 ENDCASE")
direct("RUN")
program('10 CASE 1 OF', '20 WHEN "a": PRINT', "30 ENDCASE")
direct("RUN")
direct("UNTIL TRUE")
direct("ENDWHILE")
direct('CASE 1 OF WHEN 1: PRINT')
program("10 WHILE FALSE", "20 PRINT")
direct("RUN")
program("10 IF FALSE THEN", "20 PRINT (", "30 ENDIF")
direct("RUN")
direct("REPEAT X%+=1: UNTIL X%>4: PRINT X%")
direct('X%=0: REPEAT UNTIL TRUE: PRINT "REPEAT UNTIL, no colon"')

# PROC and FN (step 3d): parameters, LOCAL, RETURN parameters, recursion,
# one-line FNs, an FN's string value, DEF skipped as the program runs,
# loops left inside a call, END inside one.
run("PROC and FN",
    '10 x = 1: PROCshow("one", x): PRINT "x still "; x',
    "20 PRINT FNsq(7), FNfact(10), FNfact(12)",
    '30 PRINT FNgreet$("Ada"); "|"; FNtwice(21); "|"; FNgreet$("x") + FNgreet$("y")',
    "40 a% = 5: b% = 6: PROCswap(a%, b%): PRINT a%; b%",
    "50 PROCloop: PRINT \"after PROCloop\"",
    "60 n = 3: PROCcount(n): PRINT \"n is \"; n",
    "70 PRINT FNsum(4) + 1",
    "80 END",
    '100 DEF PROCshow(t$, x)',
    '110   LOCAL y',
    '120   x = x * 10: y = 2',
    '130   PRINT t$; " "; x; " "; y',
    "140 ENDPROC",
    "150 DEF FNsq(v) = v * v",
    "160 DEF FNfact(n)",
    "170   IF n <= 1 THEN = 1",
    "180 = n * FNfact(n - 1)",
    '190 DEF FNgreet$(n$) = "Hello, " + n$',
    "200 DEF FNtwice(v) = v + v",
    "210 DEF PROCswap(RETURN p%, RETURN q%)",
    "220   LOCAL t%: t% = p%: p% = q%: q% = t%",
    "230 ENDPROC",
    "240 DEF PROCloop",
    "250   FOR i% = 1 TO 10: IF i% = 3 THEN ENDPROC",
    "260   NEXT",
    "270 ENDPROC",
    "280 DEF PROCcount(RETURN c): c += 1: ENDPROC",
    "290 DEF FNsum(k)",
    "300   LOCAL t, j: FOR j = 1 TO k: t += j: NEXT",
    "310 = t")

print("== calls' mistakes")
program("10 PROCnone", "20 END")
direct("RUN")
check(b.err == 29, "No such FN/PROC is 29: %d" % b.err)
program("10 PROCp(1, 2)", "20 END", "30 DEF PROCp(a): ENDPROC")
direct("RUN")
program("10 PRINT FNq", "20 END", "30 DEF FNq", "40 ENDPROC")
direct("RUN")
direct("ENDPROC")
direct("= 1")
direct("LOCAL z")
program('10 PROCp("text")', "20 END", "30 DEF PROCp(a): ENDPROC")
direct("RUN")
program("10 PROCp", "20 PRINT \"not here\"", "30 DEF PROCp: END")
direct("RUN")
program("10 PRINT FNinf(1)", "20 DEF FNinf(n) = FNinf(n + 1)")
direct("RUN")
program("10 PROCpart", "20 DEF PROCpart", '30 PRINT "no ENDPROC"')
direct("RUN")

# Python's functions, for a PROC or FN the program does not define.
print("== calling Python")
ns["shout"] = lambda t: print(t.upper() + "!")
ns["half"] = lambda x: x / 2
ns["join"] = lambda a, b: a + "-" + b
ns["is_big"] = lambda x: x > 100
ns["fails"] = lambda: 1 // 0
ns["thing"] = lambda: [1, 2]
direct('PROCshout("hello"): PRINT FNhalf(9); " "; FNjoin("a", "b"); " "; FNis_big(500)')
direct("PRINT FNfails")
check(b.report_text == "ZeroDivisionError: divide by zero" or
      b.report_text.startswith("ZeroDivisionError"), "the exception's message: %r" % b.report_text)
direct("PRINT FNthing")
direct('A$ = FNhalf(2)')

# ON ERROR: the handler runs, ERR, ERL and REPORT tell what came; ON ERROR
# OFF; ON ERROR LOCAL inside a PROC, LOCAL ERROR and RESTORE ERROR; ERROR
# raises the program's own; Escape is caught, but not twice in a second.
run("ON ERROR",
    '10 ON ERROR PRINT "caught "; ERR; " at "; ERL; ": "; REPORT$: GOTO 40',
    "20 PRINT 1 / 0",
    '30 PRINT "not here"',
    '40 ON ERROR OFF',
    '50 ON ERROR PRINT "mine: "; ERR; " "; : REPORT: PRINT: GOTO 70',
    '60 ERROR 100, "my error"',
    '70 PROCsafe: PRINT "after PROCsafe"',
    "80 ON ERROR OFF",
    "90 PRINT nosuch",
    "100 DEF PROCsafe",
    "110   LOCAL ERROR",
    '120   ON ERROR LOCAL PRINT "local: "; REPORT$: ENDPROC',
    "130   FOR i = 1 TO 3",
    "140     PRINT SQR(-i)",
    "150   NEXT",
    "160 ENDPROC")

b.getkey = lambda: (_ for _ in ()).throw(KeyboardInterrupt)  # ESC, whenever asked
program('10 ON ERROR IF ERR = 17 THEN PRINT "Escape caught": GOTO 20',
        "20 K = INKEY(0): GOTO 20")
direct("RUN")
b.getkey = host.getkey

# Star commands and OSCLI (step 3e): OS commands, BBC names mapped; the
# host runs them (here a stand-in that shows the line).
print("== star commands")
b.oscli = lambda line: print("[os] " + line)
direct("*CAT")
direct("*.")
direct("*cat sys:")
direct("*DIR df0:")
direct("*TYPE notes.txt")
direct("*echo P. stays as typed")
direct('OSCLI "DELETE old.bas"')
direct('OSCLI "*COPY a b"')
program("10 *RENAME a.txt b.txt", '20 PRINT "after"')
direct("LIST")
direct("RUN")
direct('X=1:*CAT')

# SAVE, LOAD and CHAIN, with BBC's header; LOAD in a program ends it, CHAIN
# runs the new one with A% to Z% kept; a Classic file is KoraCLI's to ask.
print("== files")
import os
DIR = "/tmp/koracli-bbc-test"
try:
    os.mkdir(DIR)
except OSError:
    pass
os.chdir(DIR)
for f in os.listdir():
    os.remove(f)
program('10 PRINT "first"; A%', '20 CHAIN "SECOND"')
direct('SAVE "FIRST"')
with open("FIRST.BAS") as f:
    text = f.read()
check(text == '0 REM !KoraBasic:BBC\n10 PRINT "first"; A%\n20 CHAIN "SECOND"\n',
      "the file: %r" % text)
program('10 PRINT "second"; A%', '20 LOAD "FIRST"', '30 PRINT "not here"')
direct('SAVE "SECOND"')
direct("NEW")
direct('A% = 7: B = 2: LOAD "FIRST"')
direct("PRINT B")
direct("RUN")
direct("LIST")
direct('CHAIN "SECOND"')
direct('LOAD "NOSUCH"')
direct("SAVE")
with open("OLD.BAS", "w") as f:
    f.write("0 REM !KoraBasic:Classic\n10 PRINT 1\n")
try:
    direct('CHAIN "OLD"')
    check(False, "a Classic file: KoraCLI asks to switch")
except basic.OtherDialect as e:
    print("other dialect:", e.dialect, e.name, e.run)
with open("BAD.BAS", "w") as f:
    f.write("PRINT\n")
direct('LOAD "BAD"')
os.chdir("/")
for f in os.listdir(DIR):
    os.remove(DIR + "/" + f)
os.rmdir(DIR)

# RENUMBER: GOTO, GOSUB, ON's lists, THEN, ELSE, RESTORE, joined or not; a
# computed GOTO left alone; a line not there warned about. AUTO.
print("== RENUMBER and AUTO")
program("5 ON K% GOSUB 17, 23 ELSE PRINT", "17 IF K% THEN 23 ELSE 31",
        "23 RESTORE 31: GOTO 17+K%", "31 DATA 1: GOSUB99 is data", "35 GOSUB99",
        "40 IFX>1THEN5ELSE17")
direct("RENUMBER")
direct("LIST")
direct("RENUMBER 100, 5")
direct("LIST ,105")
direct("AUTO 200, 20")
check(b.auto_text() == "200 ", "AUTO's first number: %r" % b.auto_text())
b.enter("200 PRINT")
check(b.auto_text() == "220 ", "and the next: %r" % b.auto_text())
b.enter("220")
check(b.auto is None, "the number alone ends AUTO")

# LISTO: 0 no space after the number, 2 the constructs indented.
print("== LISTO")
program("10 FOR I%=1 TO 2", "20 IF I%=1 THEN", '30 PRINT "one"', "40 ELSE",
        '50 REPEAT: PRINT "x";: UNTIL TRUE', "60 ENDIF", "70 NEXT", "80 FOR J%=1 TO 2: NEXT",
        "90 END")
direct("LISTO 0: LIST ,20")
direct("LISTO 3: LIST")
direct("LISTO 1")

# LIST: line numbers in five columns; LIST 20,30 a range.
print("== LIST")
program("10 REM one", "20 PRINT 2", "30 PRINT 3", "1000 END")
direct("LIST")
direct("LIST 20,30")
direct("LIST ,20")

print("koracli bbc: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
