# SPDX-License-Identifier: GPL-3.0-or-later
#
# EL0 regression fixture for MicroPython's port (project_doc/koracli-plan.md,
# step 0b), run by the smoke test as `micropython sys:tests/mpprobe.py ...`.
#
#   mpprobe.py files A B   the port's essentials: sys.argv and sys.path, import
#                          from the script's directory, os over volumes and
#                          assigns, the kora module, time, a heap that grows,
#                          the stack check, and on a writable boot volume a
#                          file written, read, renamed and removed. Prints
#                          "mpprobe: ok" and ends with sys.exit(3).
#   mpprobe.py break       Ctrl+C as KeyboardInterrupt: caught in a loop, then
#                          in a sleep, then left uncaught in a loop (exit 130).
#   mpprobe.py repl        kora.repl(), KoraCLI's fallback: MicroPython's own
#                          REPL inside the script, until Ctrl+D, then
#                          "mpprobe: after the REPL".
#
# A failed check prints "mpprobe: FAILED ..." and the script exits with 1.
import sys

import gc
import kora
import os
import time


def check(ok, what):
    if not ok:
        print("mpprobe: FAILED", what)
        sys.exit(1)


def files():
    check(sys.argv == ["sys:tests/mpprobe.py", "files", "A B"], "sys.argv %r" % (sys.argv,))
    check(sys.path[0] == "sys:tests" and "sys:python" in sys.path, "sys.path %r" % (sys.path,))
    import mpprobe_mod

    check(mpprobe_mod.double(mpprobe_mod.ANSWER) == 84, "a module from the script's directory")

    names = os.listdir("sys:tests")
    check("mpprobe.py" in names and "mpprobe_mod.py" in names, "listdir %r" % (names,))
    st = os.stat("sys:tests/mpprobe.py")
    check(st[0] == 0x8000 and st[6] > 1000, "stat of a file %r" % (st,))
    check(os.stat("sys:tests")[0] == 0x4000, "stat of a directory")
    try:
        os.stat("sys:tests/nosuch")
        check(False, "stat of nothing")
    except OSError:
        pass
    cwd = os.getcwd()
    os.chdir("sys:tests")
    check("mpprobe.py" in os.listdir(), "listdir of the current directory after chdir")
    os.chdir(cwd)
    check(os.getcwd() == cwd, "getcwd after chdir back")

    volumes = kora.volumes()
    boot = [v for v in volumes if v[2]]
    check(len(boot) == 1, "one boot volume in %r" % (volumes,))
    check("sys" in [a[0].lower() for a in kora.assigns()], "the sys: assign in %r" % (kora.assigns(),))
    check(kora.uptime() > 0, "uptime")

    start = time.ticks_ms()
    time.sleep_ms(50)
    check(time.ticks_diff(time.ticks_ms(), start) >= 50, "sleep_ms and ticks")

    # Far more than the first 256 KB of heap: it grows, then gives it back.
    big = bytearray(3 * 1024 * 1024)
    big[-1] = 7
    check(len(big) == 3 * 1024 * 1024 and big[-1] == 7, "3 MB on a growing heap")
    big = None
    gc.collect()

    def deep(n):
        return deep(n + 1) + 1

    try:
        deep(0)
        check(False, "deep recursion")
    except RuntimeError:
        pass

    if boot[0][3]:
        print("mpprobe: read-only boot volume, nothing written")
    else:
        with open("sys:mpprobe.txt", "w") as f:
            f.write("first line\n")
            f.write("café\n")
        with open("sys:mpprobe.txt") as f:
            check(f.readlines() == ["first line\n", "café\n"], "text read back")
        with open("sys:mpprobe.txt", "rb") as f:
            f.seek(6)
            check(f.read(4) == b"line" and f.tell() == 10, "seek, read and tell, binary")
        with open("sys:mpprobe.txt", "a") as f:
            f.write("appended\n")
        check(os.stat("sys:mpprobe.txt")[6] == 26, "size after append")
        os.mkdir("sys:mpprobe.dir")
        os.rename("sys:mpprobe.txt", "sys:mpprobe.dir/moved.txt")
        check(os.listdir("sys:mpprobe.dir") == ["moved.txt"], "rename into a directory")
        os.remove("sys:mpprobe.dir/moved.txt")
        os.rmdir("sys:mpprobe.dir")
        check("mpprobe.dir" not in os.listdir("sys:"), "removed again")
        os.sync()
        print("mpprobe: files written, read, renamed and removed")
    try:
        open("sys:tests/nosuch.txt")
        check(False, "open of nothing")
    except OSError:
        pass
    print("mpprobe: ok")
    sys.exit(3)


def breaks():
    print("mpprobe: looping")
    try:
        while True:
            pass
    except KeyboardInterrupt:
        print("mpprobe: KeyboardInterrupt in a loop")
    print("mpprobe: sleeping")
    try:
        time.sleep(100)
        check(False, "a 100 s sleep")
    except KeyboardInterrupt:
        print("mpprobe: KeyboardInterrupt in a sleep")
    print("mpprobe: looping, not catching it")
    while True:
        pass


if sys.argv[1:2] == ["files"]:
    files()
elif sys.argv[1:2] == ["break"]:
    breaks()
elif sys.argv[1:2] == ["repl"]:
    kora.repl()
    print("mpprobe: after the REPL")
else:
    print("usage: mpprobe.py files A B | break | repl")
    sys.exit(2)
