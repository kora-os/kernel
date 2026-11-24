# SPDX-License-Identifier: GPL-3.0-or-later
#
# A first script for the unix port of the pinned MicroPython
# (tests/run-micropython-tests.sh): the language as KoraOS's port has it, and
# where MicroPython differs from CPython, which the KoraCLI code tested here
# later has to live with.

print(1 + 1)
print([x * x for x in range(5)], 2 ** 70, 1 / 4)


def gen(n):
    for i in range(n):
        yield i * 10


print(list(gen(3)))
print({"a": 1, "b": 2}["b"], "ok" if 3 > 2 else "no")
print("%s-%d" % ("kora", 42), "{:>5}".format("py"))
try:
    raise ValueError("boom")
except ValueError as e:
    print("caught", e)
print(int("ff", 16), str(b"abc", "utf-8"), len("é€"))
