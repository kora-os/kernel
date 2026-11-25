# SPDX-License-Identifier: GPL-3.0-or-later
#
# KoraCLI's `!` (user/koracli/bang.py) and kora's command helpers
# (user/python/kora.py) on the unix port: the command line's words, quoting,
# {expr}, redirection and suffixes; blocks with their ! lines translated; the
# arguments a translated line gives at run time; wrappers.

import bang
import kora

failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print("FAIL", what)


def raises(fn, what):
    try:
        fn()
    except bang.CommandError:
        return
    check(False, what)


# Words and quoting.
c = bang.parse("cat  a.txt 'b c.txt' \"d\"e")
check(c["words"] == [["cat"], ["a.txt"], ["b c.txt"], ["de"]], "words, quotes join: %r" % c["words"])
check(c["stdin"] is None and c["stdout"] is None and not c["background"] and c["core"] is None,
      "nothing else")
check(bang.parse("echo ''")["words"] == [["echo"], [""]], "an empty quoted argument")
c = bang.parse('rm a\\ b.txt \\& \'\\x\' "q\\""')  # rm a\ b.txt \& '\x' "q\""
check(c["words"] == [["rm"], ["a b.txt"], ["&"], ["\\x"], ['q"']] and not c["background"],
      "backslash: escapes a space and &, literal in '...', escapes a quote in \"...\": %r"
      % c["words"])
raises(lambda: bang.parse("echo a\\"), "a backslash at the end")

# {expr}, {{ and }}.
c = bang.parse("cat {files} file{n}.txt {d['k']} {{x}}")
check(c["words"] == [["cat"], [("expr", "files")], ["file", ("expr", "n"), ".txt"],
                     [("expr", "d['k']")], ["{x}"]], "interpolation: %r" % c["words"])
check(bang.parse("echo {x}", interpolate=False)["words"] == [["echo"], ["{x}"]],
      "OS mode keeps braces")
raises(lambda: bang.parse("echo {x"), "an unclosed {")
raises(lambda: bang.parse("echo x}"), "a lone }")
raises(lambda: bang.parse("echo {}"), "an empty {}")
raises(lambda: bang.parse("echo 'x"), "an unclosed quote")

# Redirection and suffixes.
c = bang.parse("sort < in.txt >> out.txt @2 &")
check(c["words"] == [["sort"]] and c["stdin"] == ["in.txt"] and c["stdout"] == ["out.txt"] and
      c["append"] and c["background"] and c["core"] == 2, "redirection, append, @2 and &")
c = bang.parse("cat >out.txt a")
check(c["words"] == [["cat"], ["a"]] and c["stdout"] == ["out.txt"] and not c["append"],
      "a redirection glued to its file")
c = bang.parse("mail bob@example.com '&'")
check(c["words"] == [["mail"], ["bob@example.com"], ["&"]] and not c["background"],
      "only bare trailing & and @N are suffixes")
raises(lambda: bang.parse("cat <"), "a redirection without its file")
raises(lambda: bang.parse("&"), "no command")

# Translation of ! lines, at a statement's start only.
t = bang.translate("!ls")
check(t == "kora._bang(False, [('ls',)])", "!ls: %s" % t)
t = bang.translate("out = !cat {f} > x")
check(t == "out = kora._bang(True, [('cat',), ((f,),)], stdout=('x',))", "capture: %s" % t)
t = bang.translate("for f in files:\n    !cat {f}\n    n = len(f)")
check(t == "for f in files:\n    kora._bang(False, [('cat',), ((f,),)])\n    n = len(f)",
      "inside a block: %s" % t)
t = bang.translate("x = [1,\n!2]")
check(t == "x = [1,\n!2]", "not inside open brackets")
check(bang.translate("a != b") == "a != b" and bang.translate("a == !b") == "a == !b",
      "comparisons are left alone")
check(bang.translate("x.y = !pwd") == "x.y = kora._bang(True, [('pwd',)])", "a dotted name")

# What a translated line gives at run time.
files = ["a b", "c"]
n = 2
words = [("cat",), ((files,),), ("file", (n,), ".txt")]
check(kora._argv(words) == ["cat", "a b", "c", "file2.txt"],
      "a list alone expands, a value in a word joins: %r" % kora._argv(words))
try:
    kora._argv([("x", (files,))])
    check(False, "a list inside a word")
except TypeError:
    pass
check(kora._argv([((("p", "q"),),), ((3,),)]) == ["p", "q", "3"], "a tuple, a number")

# Wrappers: found by name, never for a path; the result shown at the prompt.
@kora.cmd.register
def greet(*args):
    return "hi " + " ".join(args)


check(kora.cmd.find("greet") is greet and kora.cmd.greet is greet, "a registered wrapper")
check(kora.cmd.find("c:greet") is None and kora.cmd.find("/bin/greet") is None,
      "a path is never a wrapper")
check(kora.run("greet", "you") == "hi you" and kora.capture("greet", "x") == "hi x",
      "run and capture call it")
kora._bang(False, [("greet",), ("there",)])  # prints its result
kora.cmd.register(lambda *a: len(a), "count")
check(kora.cmd.names() == ["count", "greet"], "wrappers by name")
kora.cmd.unregister("count")
check(kora.cmd.find("count") is None, "and taken away")

# The captured result.
r = kora._captured("one\ntwo\n", 3, "err")
check(r == "one\ntwo\n" and r.status == 3 and r.lines == ["one", "two"] and r.stderr == "err" and
      isinstance(r, str) and r.upper() == "ONE\nTWO\n", "a str with status, lines and stderr")

print("koracli bang: %s" % ("all passed" if failures == 0 else "%d FAILED" % failures))
