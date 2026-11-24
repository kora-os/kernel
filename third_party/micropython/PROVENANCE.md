# Vendored: MicroPython

This directory holds a **subset** of [MicroPython](https://micropython.org/),
the Python 3 implementation for microcontrollers by Damien P. George and
contributors, vendored as KoraCLI's Python (see the project
[CREDITS](../../CREDITS.md) and `project_doc/koracli-plan.md`).

- **License:** MIT (see `LICENSE`), compatible with KoraOS's
  GPL-3.0-or-later. Every file keeps its copyright header.
- **Upstream:** https://github.com/micropython/micropython
- **Release:** v1.29.0 (2026-08-24), tag `v1.29.0`, commit
  `0fd6c573ea815774668bbb16b8e197c8822368b2`, taken from GitHub's source
  tarball of the tag (sha256
  `8072f6ad42c0e3bbfca88ed09bf8f1106ce207a39129af920f70081b8f2fe286`).

## What was vendored

Upstream files, unchanged:

- `py/`: the whole core, with its build scripts (`makeqstrdefs.py`,
  `makeversionhdr.py` and the rest) and build files (`py.cmake`, `py.mk`,
  `mkrules.*`).
- `shared/readline/` (the REPL's line editor) and `shared/runtime/` (the
  REPL, `pyexec`, and the GC's register helpers), whole.
- `ports/unix/`, whole: the host interpreter the Python tests run on
  (`tests/run-micropython-tests.sh`).
- What the unix port's build reads besides: the `extmod/` sources it
  compiles (most are empty under its configuration) and the headers they
  include, `extmod/extmod.mk` and `extmod/extmod.cmake`,
  `shared/libc/abort_.c` and `printf.c`, `shared/timeutils/`, and
  `lib/oofatfs/ff.h` and `ffconf.h`.
- `LICENSE`.

The list is what the unix port's build, with KoraOS's variant, actually
read (its dependency files), plus `py/` and `shared/runtime` whole. Nothing
else of the repository is here: no other ports, no `lib/` submodules, no
`tests/`, `tools/`, `mpy-cross/` or docs.

## How it is built

- **On KoraOS:** `cmake/micropython.cmake` builds the core with the port in
  `user/micropython` (which is KoraOS's own code, GPL-3.0-or-later) into
  the `micropython` program. It takes the source list from `py/py.cmake`
  and runs the generator scripts in the order of `py/mkrules.cmake`,
  giving them the version from the pinned tag (`MICROPY_GIT_TAG`), since
  `git describe` here would describe KoraOS's repository.
- **On the host:** `tests/run-micropython-tests.sh` builds `ports/unix` with
  the variant in `user/micropython/unix/koraos`, outside this directory,
  into `build/`.

## Local patches

None. Where upstream code does not suit KoraOS, the port works around it:

- `shared/runtime/gchelper_generic.c` reads AArch64's callee-saved
  registers through local register variables, which clang does not
  guarantee to read: the port uses the setjmp variant
  (`MICROPY_GCREGS_SETJMP`).
- The exact float repr needs `long double`, software binary128 on AArch64,
  with no library for it: the port uses the approximate one.
- `ports/unix/main.c` calls a thread function on macOS even without threads
  (as of v1.29.0): there the host interpreter is built with threads.

## Updating

Replace the files above with those of a newer release (the same paths;
rebuild the unix port and compare its dependency files for new ones), then
update the tag, commit and date here, in `cmake/micropython.cmake` and in
the smoke test's banner check, and run `tests/run-micropython-tests.sh` and
the QEMU smoke test.
