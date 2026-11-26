# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's Classic BASIC (user/koracli/classic.py, basic.py) on the unix
# port: the program store, numbers as BASIC prints them, and type-in listings
# whose output (in koracli_basic.py.exp) is what a C64 would print, with the
# GW-BASIC additions; the errors, STOP and ESC with CONT, INPUT and GET.

import basic
import classic

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
        self.text = []

    def write(self, s):
        self.text.append(s)
        print(s, end="")

    def readline(self):
        line = self.lines.pop(0) if self.lines else ""
        if line is KeyboardInterrupt:
            raise KeyboardInterrupt
        print(line)  # as the line editor echoes it
        return line

    def getkey(self):
        k = self.keys.pop(0) if self.keys else ""
        if k is KeyboardInterrupt:
            raise KeyboardInterrupt  # ESC, while the program polls the keys
        return k


host = Host()
b = classic.Interpreter(host.write, host.readline, host.getkey, 40)


def program(*lines):
    b.execute("NEW")
    for line in lines:
        b.enter(line)


def run(title, *lines):
    print("== " + title)
    program(*lines)
    b.execute("RUN")


def direct(text):
    print("> " + text)
    b.execute(text)


# The program store: lines by number, keywords in capitals, as typed otherwise.
p = basic.Program(classic.normalize)
p.enter("20 print \"b\"")
p.enter("10 ?x;y")
p.enter("15 fori=1to3:next ' count")
check(p.lines == {10: "PRINT x;y", 15: "FORi=1TO3:NEXT ' count", 20: "PRINT \"b\""},
      "stored with keywords in capitals: %r" % p.lines)
p.enter("15")
check(sorted(p.lines) == [10, 20], "a number alone deletes its line")
check(classic.normalize("?a") == "PRINT a" and classic.normalize("rem Print") == "REM Print"
      and classic.normalize('data print, "x"') == 'DATA print, "x"',
      "? is PRINT; REM and DATA keep what follows")
try:
    p.enter("64000 PRINT")
    check(False, "a line number past 63999")
except basic.BasicError as e:
    check(e.name == "SYNTAX", "a line number past 63999")

# Numbers as PRINT and STR$ give them.
for x, want in ((0, " 0"), (1, " 1"), (-1, "-1"), (0.5, " .5"), (-0.25, "-.25"),
                (1 / 3, " .333333333"), (2 / 3, " .666666667"), (123456789, " 123456789"),
                (1e9, " 1E+09"), (1234567890, " 1.23456789E+09"), (0.01, " .01"),
                (0.001, " 1E-03"), (-1.5e-10, "-1.5E-10"), (3.14159265358979, " 3.14159265"),
                (100, " 100"), (1e38, " 1E+38"), (999999999.6, " 1E+09")):
    check(classic.num_str(x) == want, "num_str(%r) = %r, not %r" % (x, classic.num_str(x), want))
check(classic.val(" 12AB") == 12 and classic.val("-3.5E2X") == -350 and classic.val("X") == 0 and
      classic.val("&HFF") == 255, "VAL takes the number at the start")
check(classic.parse_number(" 42 ") == 42 and classic.parse_number("4 2") is None and
      classic.parse_number("") == 0, "an INPUT or DATA item is all number")

# Type-in listings.
run("loops, zones, TAB and SPC",
    "10 FORI=1TO3:PRINTI;:NEXT:PRINT",
    "20 FOR I = 10 TO 1 STEP -3: PRINT I,: NEXT I: PRINT",
    "30 FOR I=1 TO 2: FOR J=1 TO 2: PRINT I*10+J;: NEXT J,I: PRINT",
    '40 PRINT "A";TAB(5);"B";SPC(3);"C"',
    '50 PRINT "X"1"Y": PRINT 1;-1;"END"',
    "60 PRINT -2^2;2^-1;2^3^2;7 MOD 3;-7 MOD 3;7\\2;5 AND 3;5 OR 3;NOT 0;1<2;\"A\">\"B\"")

run("names: keywords inside them, and case",
    "10 TOTAL = 0: FOR N = 1 TO 10: TOTAL = TOTAL + N: NEXT N",
    '20 FORMAT$ = "ok": NOTE = 7: score = 3',
    '30 PRINT TOTAL; FORMAT$; NOTE; SCORE; Score',
    '40 a$ = "mixed": PRINT A$')

run("GOSUB, ON, IF and ELSE",
    "10 FOR K = 1 TO 3: ON K GOSUB 100, 200, 300: NEXT",
    '20 IF K > 3 THEN PRINT "K IS"; K ELSE PRINT "NO"',
    '30 IF K < 3 THEN PRINT "NO" ELSE IF K = 4 THEN PRINT "FOUR" ELSE PRINT "NO"',
    '40 IF 0 THEN PRINT "NOT THIS": PRINT "NOR THIS"',
    '50 IF 1 GOTO 70',
    '60 PRINT "SKIPPED"',
    '70 X = 2: ON X GOTO 80, 90: PRINT "NONE"',
    '80 PRINT "EIGHTY"',
    '90 PRINT "NINETY": END',
    '100 PRINT "ONE": RETURN',
    '200 PRINT "TWO": RETURN',
    '300 PRINT "THREE": RETURN')

run("arrays",
    "10 DIM A(3, 2): FOR I = 0 TO 3: FOR J = 0 TO 2: A(I, J) = I * J: NEXT J, I",
    "20 PRINT A(3, 2); A(2, 1): B(10) = 5: PRINT B(10); B(0)",
    '30 DIM N$(2): N$(1) = "ONE": PRINT N$(1); LEN(N$(2))',
    "40 PRINT C(11)")

run("DATA, READ and RESTORE",
    '10 READ A$, B, C$: PRINT A$; B; C$',
    '20 RESTORE: READ X$: PRINT X$',
    '30 RESTORE 60: READ D: PRINT D',
    '40 DATA "HELLO, WORLD", 42, plain words',
    '50 DATA 1E3',
    '60 DATA 7, , -1',
    '70 READ E, F: PRINT E; F: READ G')

run("READ a word into a number",
    "10 READ A",
    "20 DATA NOT A NUMBER")

run("functions",
    "10 DEF FNSQ(X) = X * X: DEF FN CUBE(X) = X * FNSQ(X): X = 5",
    "20 PRINT FNSQ(12); FNCUBE(3); X",
    '30 PRINT LEFT$("ABCDEF", 2); MID$("ABCDEF", 3, 2); MID$("ABCDEF", 5); RIGHT$("ABCDEF", 1)',
    '40 PRINT LEN("ABC"); ASC("A"); CHR$(66); VAL("12"); STR$(-5); STR$(5)',
    '50 PRINT INSTR("HELLO", "L"); INSTR(4, "HELLO", "L"); STRING$(3, "*"); STRING$(2, 65)',
    '60 PRINT HEX$(255); HEX$(-1); &HFF; SPACE$(2); "|"; ABS(-3); SGN(-2); INT(-2.5); INT(2.5)',
    '70 PRINT SQR(16); EXP(0); LOG(1); SIN(0); COS(0); ATN(0)',
    '80 A = RND(-1): R1 = RND(1): A = RND(-1): PRINT RND(1) = R1; RND(1) < 1')

run("WHILE and WEND",
    "10 I = 0: WHILE I < 3: I = I + 1: J = 0",
    "20 WHILE J < I: J = J + 1: PRINT J;: WEND: PRINT: WEND",
    "30 WHILE 0: PRINT \"NEVER\": WEND: PRINT \"DONE\"")

run("integers and SWAP",
    "10 A% = 1.9: B% = -1.9: C = 3: D = 4: SWAP C, D: PRINT A%; B%; C; D",
    "20 A% = 3E9")

run("a sieve of Eratosthenes",
    "10 N = 50: DIM P(N)",
    "20 FOR I = 2 TO N: IF P(I) THEN 50",
    "30 PRINT I;",
    "40 IF I * I <= N THEN FOR J = I * I TO N STEP I: P(J) = 1: NEXT J",
    "50 NEXT I: PRINT")

run("a bubble sort",
    "10 DIM A(5): FOR I = 1 TO 5: READ A(I): NEXT",
    "20 FOR I = 1 TO 4: FOR J = 1 TO 5 - I",
    "30 IF A(J) > A(J + 1) THEN T = A(J): A(J) = A(J + 1): A(J + 1) = T",
    "40 NEXT J, I",
    "50 FOR I = 1 TO 5: PRINT A(I);: NEXT: PRINT",
    "60 DATA 5, 3, 9, 1, 7")

# Errors, by line.
for title, line in (("a syntax error", "10 PRINT (1"),
                    ("a type mismatch", '10 A = "X"'),
                    ("an undefined line", "10 GOTO 99"),
                    ("NEXT without FOR", "10 NEXT"),
                    ("RETURN without GOSUB", "10 RETURN"),
                    ("division by zero", "10 PRINT 1 / 0"),
                    ("an illegal quantity", "10 PRINT SQR(-1)"),
                    ("a function not defined", "10 PRINT FNX(1)"),
                    ("WEND without WHILE", "10 WEND"),
                    ("WHILE without WEND", "10 WHILE 1"),
                    ("an array dimensioned twice", "10 DIM A(2): DIM A(3)"),
                    ("an overflow", "10 PRINT 1E300 * 1E300")):
    run(title, "5 PRINT \"LINE 5\"", line)

# Typed without a number.
print("== direct")
program('10 PRINT "TEN"', "20 PRINT \"TWENTY\": STOP", '30 PRINT "THIRTY"')
direct("PRINT 2+2: PRINT \"A\" + \"B\"")
direct('PRINT "A" + 1')
direct("GOTO 99")
direct("CONT")
direct("FOR I = 1 TO 3: PRINT I;: NEXT: PRINT")
direct("RUN")
direct("PRINT \"STOPPED, THEN\": CONT")
direct("CONT")
direct("RUN 30")
direct("GOTO 20")
direct("LIST")
direct("LIST 20")
direct("LIST -20")
direct("LIST 20-")
b.enter("20")
direct("LIST")
direct("INPUT A")
direct("PRINT 10 GOTO")

# ESC (a KeyboardInterrupt while the program runs) and CONT.
print("== ESC and CONT")
program("10 N = N + 1: GET K$: IF K$ = \"\" THEN 10", "20 PRINT \"KEY \"; K$; N")
host.keys = ["", "", KeyboardInterrupt, "", "Q"]
b.execute("RUN")
direct("PRINT N")
direct("CONT")
host.keys = ["", KeyboardInterrupt]
b.execute("RUN")
b.enter("30 REM AN EDIT")
direct("CONT")

# INPUT and GET.
print("== INPUT")
program('10 INPUT "NAME"; N$: INPUT "AGE, CITY", A, C$',
        '20 PRINT N$; A; C$: INPUT X, Y: PRINT X + Y',
        '30 INPUT Z: PRINT Z')
host.lines = ["ADA", "36", "LONDON", "1, 2, 3", "X", "4"]
b.execute("RUN")
host.lines = ["ADA", "abc, PARIS", "36, PARIS", "1", "2", ""]
b.execute("RUN")
check(host.lines == [], "every line read: %r" % host.lines)

print("== GET")
program("10 GET A$: IF A$ = \"\" THEN 10",
        "20 GET B: PRINT A$; B; ASC(A$)")
host.keys = ["", "x", "7"]
b.execute("RUN")
check(classic.key_char("", 0x28) == "\r" and classic.key_char("", 0x52) == chr(145) and
      classic.key_char("a", 0x04) == "a", "keys as GET gives them: Return, Up")

print("koracli basic: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
