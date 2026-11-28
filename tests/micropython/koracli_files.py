# SPDX-License-Identifier: GPL-3.0-or-later
#
# Classic BASIC's files and line tools (user/koracli/classic.py, spec §12)
# on the unix port: SAVE and LOAD with the dialect's header as the file's
# line 0, `.BAS` added, the file's mistakes, LOAD in a program (chaining);
# RENUM with the lines named by GOTO, GOSUB, ON, THEN, ELSE, RESTORE and
# RUN, line 0 staying; AUTO's numbers.

import os

import basic
import classic

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


ns = {}
b = classic.Interpreter(lambda s: print(s, end=""), ns=ns)


def program(*lines):
    b.execute("NEW")
    for line in lines:
        b.enter(line)


def direct(text):
    print("> " + text)
    b.execute(text)


def read(name):
    with open(name) as f:
        return f.read()


def write(name, text):
    with open(name, "w") as f:
        f.write(text)


# A directory of the test's own.
DIR = "/tmp/koracli-files-test"
try:
    os.mkdir(DIR)
except OSError:
    pass
os.chdir(DIR)
for name in os.listdir():
    os.remove(name)

# SAVE: the header, then the program as LIST shows it; .BAS added.
print("== SAVE and LOAD")
program('20 PRINT "WORLD"', '10 print "HELLO";', "30 X = X + 1: PRINT X")
direct('SAVE "HELLO"')
check(read("HELLO.BAS") == '0 REM !KoraBasic:Classic\n10 PRINT "HELLO";\n20 PRINT "WORLD"\n'
      '30 X = X + 1: PRINT X\n', "the file: %r" % read("HELLO.BAS"))
check(b.file == "HELLO", "the name kept for F6")
direct('N$ = "COPY.TXT": SAVE N$, 8')
check(read("COPY.TXT") == read("HELLO.BAS"), "a name with an extension, and a C64's device")

# LOAD: in place of the program, the variables cleared, the header gone.
direct("X = 41: NEW")
b.enter('5 PRINT "OLD"')
direct("X = 41")
direct('LOAD "HELLO"')
direct("LIST")
direct("PRINT X")
direct("RUN")
check(b.file == "HELLO", "the name loaded, kept")

# A program's own line 0 is saved after the header and loaded back.
program('0 REM MINE', '10 PRINT "TEN"')
direct('SAVE "ZERO"')
program()
direct('LOAD "ZERO": LIST')

# Files written by hand: no header, blank lines, keywords in any case.
write("HAND.BAS", "\n10 print \"by hand\"\r\n\n  20 goto 10 \n")
direct('LOAD "HAND": LIST')

# The file's mistakes leave the program as it was.
program('10 PRINT "KEPT"')
direct('LOAD "NOTHERE"')
write("BAD.BAS", '10 PRINT "A"\nPRINT "NO NUMBER"\n')
direct('LOAD "BAD"')
write("BIG.BAS", "64000 PRINT\n")
direct('LOAD "BIG"')
write("BBC.BAS", "0 REM !KoraBasic:BBC\n10 PRINT \"BBC\"\n")
try:
    direct('LOAD "BBC"')
    check(False, "a BBC file: KoraCLI asks to switch")
except basic.OtherDialect as e:
    print("other dialect:", e.dialect, e.name, e.run)
write("FOO.BAS", "0 REM !KoraBasic:Foo\n10 PRINT\n")
direct('LOAD "FOO"')
direct("LIST")
direct("SAVE")
direct('LOAD ""')
direct("SAVE 5")
direct('SAVE "NODIR/X"')

# LOAD in a program: the new one runs from its start, the variables kept.
print("== chaining")
write("PART2.BAS", '10 PRINT "PART 2, X ="; X\n')
program('10 X = 7: LOAD "PART2"', '20 PRINT "NOT HERE"')
direct("RUN")
direct("LIST")

# RENUM: line 0 stays, the references follow.
print("== RENUM")
program("0 REM HEADER OF MY OWN",
        "5 ON K GOSUB 17, 23: IF K THEN 23 ELSE 31",
        "17 GOTO17",
        "23 RESTORE 31: GOSUB 99",
        '31 DATA 1: PRINT "GOTO 5": REM GOTO 5',
        "40 IFX>1THEN5ELSE17",
        "41 RUN 5")
direct("RENUM")
direct("LIST")
direct("RENUM 100, 5")
direct("LIST")
direct("RENUM 1000, 0")
direct("RENUM 63990")
direct("LIST 100-110")
program("10 X = 1", "20 X = X + 1: IF X < 4 THEN 20", "30 PRINT X")
direct("RENUM 1, 1: RUN")
direct("LIST")

# AUTO: the next number, the line there if there is one; the number alone
# ends it, and any line moves it on.
print("== AUTO")
program("20 PRINT \"OLD 20\"")
direct("AUTO")
check(b.auto_text() == "10 ", "AUTO starts at 10: %r" % b.auto_text())
b.enter("10 PRINT 1")
check(b.auto_text() == '20 PRINT "OLD 20"', "the line there, to keep: %r" % b.auto_text())
b.enter('20 PRINT "OLD 20"')
check(b.auto_text() == "30 ", "on past it: %r" % b.auto_text())
b.enter("25 PRINT 2")
check(b.auto_text() == "35 ", "after a number typed: %r" % b.auto_text())
b.enter("35")
check(b.auto is None and 35 not in b.program.lines, "its number alone ends it")
direct("AUTO 63990, 5")
b.enter("63995 REM LAST")
check(b.auto is None, "past 63999 it ends")
direct("AUTO 100, 0")
direct("LIST")

os.chdir("/")
for name in os.listdir(DIR):
    os.remove(DIR + "/" + name)
os.rmdir(DIR)

print("koracli files: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
