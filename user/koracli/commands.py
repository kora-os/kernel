# SPDX-License-Identifier: GPL-3.0-or-later
#
# The built-in commands of KoraOS's shell, now KoraCLI's (KoraCLI step 1d):
# wrappers in kora.cmd, so they work in OS mode (cd), with ! in Python mode
# (!cd) and from code (kora.run("cd")). cd has to be one: it changes the
# interpreter's own directory. Their output and messages are the old
# shell's.

import os

import kora

HELP = """KoraCLI OS mode (F2 for Python):
  help            show this text
  exit            leave KoraCLI (Ctrl+D too)
  cd [path]       change directory, or print cwd
  pwd             print current directory
  volumes         list devices and volume labels
  name:           change to a device, volume or assign
  assign          list assigns
  assign name target  assign an existing directory
  assign name     remove an assign (sys: is fixed)
  ls [path]       list a directory (default current)
  cat <path>...   print file contents
  cp/rm/mkdir/rmdir/mv  manage files (cp and mv preserve existing targets)
  sync            flush writable volumes
  quote paths containing spaces
  <program> [args...]  run a program from c:
  <program> [args...] &  run it in the background
  <program> [args...] @N  run it on core N (with & too: ... @N &)
  <program> [args...] < in > out  read stdin from a file, write stdout
                  to one (>> appends); the console otherwise
  ps              list tasks and the core each runs on
  kill <pid>      end a program (a background job)
  Ctrl+C ends the program in front
programs in /bin: hello, echo, gfxdemo, termdemo, condemo, jobdemo, pindemo,
  ls, cat
  (try 'ls /bin')"""


def help(*args):
    print(HELP)


def exit(*args):
    print("bye")
    raise SystemExit


def cd(*args):
    if not args:
        print(os.getcwd())
    elif len(args) == 1:
        try:
            os.chdir(args[0])
        except OSError:
            print("cd: cannot change directory to %s" % args[0])
    else:
        print("usage: cd [path]")


def pwd(*args):
    if args:
        print("usage: pwd")
    else:
        print(os.getcwd())


def volumes(*args):
    if args:
        print("usage: volumes")
        return
    for device, label, boot, read_only in kora.volumes():
        print("%s: %s%s%s" % (device, label or "(unlabelled)", " [boot]" if boot else "",
                              " [read-only]" if read_only else ""))


def assign(*args):
    if len(args) > 2:
        print("usage: assign [name [target]]")
    elif args:
        try:
            kora.assign(*args)
        except OSError:
            print("assign: cannot update %s" % args[0])
    else:
        for name, target, fixed in kora.assigns():
            print("%s: = %s%s" % (name, target, " [fixed]" if fixed else ""))


def kill(*args):
    if len(args) != 1 or not args[0].isdigit() or int(args[0]) <= 0:
        print("usage: kill <pid>")
    elif not kora.kill(int(args[0])):
        print("kill: no such program: %s" % args[0])


def sync(*args):
    if args:
        print("usage: sync")
        return
    try:
        os.sync()
    except OSError:
        print("sync: failed")


def register():
    for fn in (help, exit, cd, pwd, volumes, assign, kill, sync):
        kora.cmd.register(fn)
