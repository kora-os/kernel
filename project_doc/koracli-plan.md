# KoraCLI plan

Status: **phase 0 done, phase 1 under way** (1a to 1c: `koracli` runs Python and OS commands); the rest is planned. Phases 0 and 1 are broken into PRs
below; the later phases are listed and get their PRs when they come up. The
plan was agreed in a discussion on 2026-11-24.

- What KoraCLI is: `koracli-spec.md` (revised in place as points settle).
- What users see: `docs/koracli.md`, the user reference, written ahead of the
  code.
- Feature status for the website: the `koracli` category of `features.yaml`.

Code references were read from the working tree on 2026-11-24 (`main` at
`9d5a27e`). Check them before relying on them.

## Today

What KoraCLI builds on:

- **The console on a screen** (`user/libscreen/console.c`): serves its
  programs' packets, with cooked and raw modes (`CON_MODE`), fonts
  (`CON_FONT`, both in `user/libc/includes/_libc/console.h`), scrollback on
  Shift+PgUp and Shift+PgDn, and the mouse wheel.
- **The shell's two-process pattern** (`console_run`): the shell opens a
  console and runs itself as that console's program. KoraCLI does the same
  with MicroPython as the program.
- **Keys to bytes** (`user/libconsole/keys.c`), built into the kernel too.
  It has no F-keys yet, and ignores Alt and Meta.
- **Line editing** (`user/libconsole/edit.c`): one line, used by the
  console's cooked mode. KoraCLI's editor is new code in Python; `edit.c`
  stays for cooked mode.
- **What the shell does today** (`user/shell.c`), which KoraCLI must keep:
  programs from `c:`, quoting, `&`, `@N`, `<`, `>`, `>>`, and the built-ins
  `cd`, `pwd`, `volumes`, `sync`, `assign`, `kill`, `help` and `exit`.
- **init** runs `screens shell` and starts it again when it ends.
- **The libc** is a complete ISO C library. What MicroPython needs on top of
  it is `libc-plan.md`'s "Before MicroPython", steps 19 to 21:

| # | Theme | Status (2026-11-24) |
|---|---|---|
| 19 | `<assert.h>`, `<inttypes.h>` | merged (#116) |
| 20 | Stack size declared at link time; `thread_new` takes a size | merged (#119) |
| 21 | A break a program can catch: opt-in, a second break ends it anyway | done (#120) |

Step 0a needs 19 and 20; step 0b needs 21.

## Decisions (2026-11-24)

- **Two processes.** A console task in C owns KoraCLI's screen: the text
  grid, the scrollback, the bars and the console port its programs write to.
  The MicroPython interpreter is an ordinary console program on that port,
  and holds the namespace, the dispatcher, BASIC and the editor. The screen
  stays responsive while Python runs, and the interpreter could run in any
  console (`newcon`, a later GUI's window).
  *Rejected:* one process with the console in a second thread, which needs a
  lock on the grid from day one and ties the interpreter to its screen.
- **The bars are console chrome**, drawn outside the text grid and set with a
  packet, so programs' output cannot overwrite them.
- **The editor is written in Python**, next to the dispatcher, so it can be
  read and changed. If it fails, the interpreter falls back to MicroPython's
  own line editor (C, `shared/readline`). Parts can move to C if they are too
  slow; a key every 100 ms or more leaves a wide margin.
- **Keys reach the editor as events**: a key-event mode of the console, next
  to cooked and raw, delivers each key's code, modifiers and character. ESC,
  the F-keys and Alt are unambiguous, with no escape sequences to parse.
  While code runs, the interpreter switches the console's "ESC means break"
  setting on.
- **Graphics start on a canvas screen of their own**, drawn by a native C
  module behind a Python interface. A program asks for a graphics surface,
  not a screen, so the later graphics layer over the console (composited,
  possibly on the GPU) changes no program.
- **MicroPython is vendored as a pinned subset** in `third_party/micropython`,
  as Circle and Spleen are (`PROVENANCE.md`, upstream files unchanged, local
  patches listed). The subset is `py/`, `shared/readline`, `shared/runtime`,
  the parts of `extmod/` used, and the unix port (minimal variant) for host
  tests. Pin the newest release when 0a starts and recheck steps 19 to 21
  against it (they were checked against v1.24.1).
  *Rejected:* a git submodule (the whole repository and its nested
  submodules, and a fork anyway for patches); fetching at build time.
- **The port lives in our tree**, `user/micropython/` (GPL-3.0-or-later):
  `mpconfigport.h`, the HAL, `main.c`, the `kora` module and the shims for
  `<unistd.h>` and `<alloca.h>`. It is built from CMake with the CMake files
  MicroPython ships for its rp2 and esp32 ports. MicroPython is MIT, which is
  compatible; CREDITS gets an entry.
- **Python code is tested on the host** with the unix port built from the
  same pinned tree, not with CPython, so MicroPython's differences show.
- **Documentation goes with the code**: every KoraCLI PR updates
  `docs/koracli.md` (each section's status, and the differences between modes
  and dialects), `features.yaml` and this plan.

## Phases

From the spec's §17, with the PRs of phases 0 and 1:

| Phase | Scope | PRs |
|---|---|---|
| 0 | MicroPython on KoraOS | 0a, 0b |
| 1 | The console's new parts; KoraCLI with Python mode | 1a, 1b |
| 1b | OS commands and OS mode; the shell replaced | 1c, 1d |
| 2 | Classic BASIC | later |
| 3 | BBC BASIC | later |
| 4 | Machine access: PEEK/POKE, indirection, `SYS`, GPIO | later |
| 5 | Graphics and sound on a canvas screen | later |
| 6 | Full-screen editor, help, F-key configuration, startup file | later |
| 7 | Job objects, `jobs`, `fg`, background output above the prompt | later |
| 8 | BASIC transpiler, F1 help panel, extras | later |

## Steps (one PR each)

- [x] **0a. MicroPython runs.** (#121) Vendor the subset with `PROVENANCE.md` and a
  CREDITS entry. Minimal port in `user/micropython`; the build generates the
  qstr headers (Python 3 is already a build dependency). A `micropython`
  program in `c:` with a REPL on its console, raw mode, using MicroPython's
  own line editor. Its main thread's stack is declared at link time (step
  20). CI builds the unix port.
  Tests: a smoke step types `print(1+1)` into `micropython` and reads `2`; a
  host test runs a trivial script on the unix port.
  *Built:* v1.29.0, the newest release (steps 19 to 21 hold for it: its
  core still includes `<unistd.h>` for `ssize_t` only, now in seven files).
  The core builds with CMake from `py/py.cmake`'s sources, and the headers
  are generated by `cmake/micropython.cmake` following `py/mkrules.cmake`,
  with the version from the tag. The host interpreter is the unix port with
  KoraOS's language (`user/micropython/unix/koraos`), and the shell sets its
  console back to cooked before each prompt, since a program killed in raw
  mode (Ctrl+C on `micropython`, until 0b) leaves it raw.
- [x] **0b. The port's essentials.** (#122) The GC heap (taken from `malloc` at
  start, size *proposed* 4 MB), the stack limit from the declared size,
  `KeyboardInterrupt` from a break (step 21), `os` over volumes and assigns
  (`listdir`, `stat`, `mkdir`, `remove`, `rename`, `getcwd`, `chdir`), files
  through the libc's stdio, `time` (`ticks_ms`, `sleep`), `sys.path` on the
  userfs, and a `kora` module with what needs no new design yet (uptime,
  volumes, assigns). Running a `.py` file: `micropython script.py`.
  Tests: smoke steps run a script from the userfs that lists a directory and
  writes a file (writable runs only), and Ctrl+C interrupts a loop with
  `KeyboardInterrupt`.
  *Built:* the heap **grows** (the user's choice, 2026-11-24): it starts at
  256 KB and MicroPython adds areas as allocations need them, doubling the
  total, each at most half the free memory, and frees areas left empty
  (`MICROPY_GC_SPLIT_HEAP_AUTO`). The break is polled by the VM at most
  every 10 ms (the counter is read in user mode, the signal syscall only
  then), and console reads go as the port's own packets so that waiting for
  a key and for the break is one `sig_wait`: Ctrl+C at the prompt is a new
  line, and no break is left pending there. `os` and files are the port's
  own over libk and the libc (no VFS); `time` is extmod's. `sys.path` is the
  script's directory, then `sys:python` (*proposed*). Also: line numbers in
  tracebacks, exit codes as CPython's (130 for Ctrl+C), and the smoke test's
  Python probes shipped in `sys:tests`.
- [x] **1a. The console's new parts.** (#123) In `user/libconsole` and
  `user/libscreen`, with libk wrappers:
  - F1 to F12 in `keys.c`, as xterm sends them, for every console;
  - the key-event mode (a new `CON_MODE` value), with Shift+PgUp and
    Shift+PgDn still kept by the console;
  - a `CON_BAR` packet: a top and a bottom bar drawn outside the grid, which
    loses a row for each;
  - the "ESC means break" setting: ESC sends a break to the program that set
    it instead of a key.

  `keys.c` is built into the kernel too; the user chose to have it merged
  on green CI all the same (2026-11-24).
  Tests: host tests for the F-key sequences (next to `tests/host/edit_test.c`)
  and the bars' layout; a small probe program in a `newcon` console prints
  the events it gets, driven by `q.key`.
  *Built:* F-keys are xterm's sequences whatever the modifiers (the event
  mode tells them apart). An event is a `struct con_key_event` (character,
  key, modifiers; key presses only, a modifier alone is none), and a read
  gets whole events; `console_events()` reads stdin's handler directly, not
  through stdio. Ctrl+C stays the console's break in event mode. A bar is a
  one-row terminal of its own in reverse video, cut at the width, kept by
  the console and drawn again when the font changes. ESC as a break goes to
  the program that asked, which must have allocated `SIG_BREAK`; it lapses
  when that program ends. `keyprobe` is the probe.
- [x] **1b. KoraCLI with Python mode.** (#124) A `koracli` program: the console task
  in C runs the interpreter as its program (`console_run`). The Python side
  lives on the userfs (location *proposed*: `sys:koracli/`): the editor
  (blocks, one blank line kept, two end the block, Ctrl+Enter, PgUp and PgDn
  block history, the C fallback), the bars (mode indicator, F-key labels),
  F2 switching between Python and OS modes (BASIC mode comes in phase 2),
  ESC as `KeyboardInterrupt` while code runs, and the dispatcher's routing at
  `>>>`: a digit stores a program line (kept, listed by nothing yet), `%`
  says BASIC is not there yet, `!` comes in 1c.
  Tests: host tests on the unix port feed key events to the editor and lines
  to the dispatcher; a smoke step starts `koracli` from the shell, types a
  two-line block and reads its output.
  *Built:* `koracli` (user/koracli.c) opens a screen and runs `micropython
  sys:koracli/main.py` on its console with `console_run`. The Python side
  is `user/koracli/*.py`, shipped to `sys:koracli` (the user's choice,
  2026-11-24): `shell.py` (the loop, bars, F1, F2, running), `editor.py`
  (pure: key events in, xterm sequences out through a `write` function,
  so it runs on the unix port), `dispatch.py`, `keys.py`. The `kora`
  module gains `key()`, `mode()`, `bar()`, `esc_break()`, `size()` and
  `repl()` (the C fallback). The console stays in key-event mode while
  code runs, so keys typed ahead stay events; the HAL turns events into
  terminal bytes (`keys.c`) for `input()`. Continuation is decided in
  Python: a compound statement goes on until two blank lines in a row, an
  open bracket or triple-quoted string until it closes; a prefixed first
  line runs at Enter. Blocks compile in `single` mode (values printed),
  falling back to `exec` for several statements.
- [x] **1c. OS commands from Python.** `kora.run` and `kora.capture` (the
  interpreter serves the child's stdout and stderr on a port of its own,
  waiting on it and on the child's end together); the captured `str`
  subclass (`.status`, `.lines`, `.stderr`); `kora.status`; resolution
  (wrapper, then `c:`, then an explicit path); `kora.cmd.register`; the
  prompt's `!` and `name = !cmd`, at `>>>` and at a statement's start inside
  blocks; arguments (quoting, `{expr}` with lists expanded, `{{ }}`);
  redirection; `&` and `@N` (keyword arguments `background`, `core`,
  `stdin`, `stdout`, `append`); Ctrl+C passed on to the foreground program.
  Tests: host tests for the argument parser and the `!` translation; smoke
  steps capture `echo` into a variable, redirect to a file and back, and run
  a job in the background.
  *Built:* the C module is `_kora` now; `kora` is `sys:python/kora.py`,
  which takes it all and adds `run`, `capture`, `status` and `cmd` in
  Python (a C module's names are fixed). `_kora.execute` spawns with
  redirection or capture ports, waits on the child's end and the break
  together (a break passed on with `sig_send`, `kill` when its main thread
  is gone), turns ESC as a break off and the console cooked for the
  child, and serves its stdout and stderr when capturing; `_kora.reap`
  collects background jobs, reported at the next prompt. `!` lines are
  translated by `sys:koracli/bang.py` into `kora._bang(...)` calls with
  each `{expr}` compiled in place (so locals work); `kora._argv` expands
  lists. OS mode lines already run as commands (no `{expr}`); its
  built-ins are 1d. Type-ahead: the console keeps queued events across
  modes and turns what was typed in cooked mode into events when the mode
  is `CON_EVENTS` again; the HAL keeps events it had read; the console
  queues 512 events; the editor writes a character typed at the line's
  end without redrawing. `fn.__name__` is on (`MICROPY_PY_FUNCTION_ATTRS`).
  Not yet: job objects (`job = !cmd &` gives the pid; phase 7).
- [ ] **1d. OS mode, and KoraCLI replaces the shell.** The `$` prompt with
  every line an OS command; the old shell's built-ins as wrappers (`cd`,
  `pwd`, `volumes`, `sync`, `assign`, `kill`, `help`, `exit`); a volume or
  assign name alone changes directory. init runs `screens koracli`;
  `user/shell.c` goes, and the smoke test, `tests/README.md` and the docs
  move to KoraCLI. Background output goes straight to the screen, as before.
  Tests: the smoke steps that drove the shell now drive OS mode, unchanged
  where possible, so parity shows in the diff.

Later phases, in a line each, to be split into PRs when they come up:

- **2. Classic BASIC.** The interpreter in Python (study PyBasic, and check its
  license before reusing code), the program store, the immediate commands,
  shared variables with the collision rules of spec §7, `SAVE` and `LOAD`
  with line 0, the dialect's errors, `READY.`. Tested on the unix port with
  type-in listings.
- **3. BBC BASIC.** The second dialect, multi-line constructs, `PROC` and
  `FN` falling back to Python, star commands, switching dialect.
- **4. Machine access.** `PEEK`, `POKE`, indirection, `SYS`, `peek`/`poke` in
  Python, the GPIO layer for Pi 3 and Pi 4.
- **5. Graphics and sound.** The native drawing module, the canvas screen,
  the BASIC keywords and Python functions.
- **6. Editing and help.** The full-screen editor (needs a packet to read a
  screen row back), the default editor style confirmed, `HELP`, `help()` and
  `?` from one source, F-key configuration, the startup file.
- **7. Jobs.** Job objects, `jobs` and `fg`; the interpreter serves
  background output and prints it above the prompt.
- **8. Performance and extras.** The BASIC transpiler, the F1 help panel,
  paren-less calls, rich wrapper objects, pipes in OS mode.

## Working rules

- One PR per step, with its tests. Each PR updates `docs/koracli.md`,
  `features.yaml` (run `tests/check-features.py`) and this plan's status.
- Before pushing, the checks of `libc-plan.md`'s pre-push list apply
  (host tests, libc tests, userfs tests, a virt smoke run), plus the unix
  port's tests once 0a adds them.

## Open decisions

(Settled 2026-11-24: steps merge on green CI unless they need testing on a
Pi, kernel-built code included.)

- **Where things live on the userfs:** the library directory on
  `sys.path` (`sys:python` for now), the user's folder for wrappers and the
  startup file. (KoraCLI's own modules are in `sys:koracli`, decided
  2026-11-24.)
- **Freezing** KoraCLI's modules into the binary later (faster start), at the
  cost of editing them on the machine.
- **Which graphics layer is on top**, for the later layer (spec §10.2).
