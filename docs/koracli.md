# KoraCLI user reference

KoraCLI is the shell KoraOS starts in: a fullscreen prompt where you can type
Python, a line of BASIC or an OS command, and see what happens.

> **Status: a first KoraCLI runs.** Type `koracli` in the shell: it opens a
> screen of its own with Python mode (step 1b). It does not replace the
> shell yet, and OS commands and BASIC are still to come. This reference is
> written ahead of the implementation and grows with it: each section says
> whether what it describes works today or which step brings it. The
> design behind it is `project_doc/koracli-spec.md`, and the steps are in
> `project_doc/koracli-plan.md`. Every change to KoraCLI updates this page in
> the same pull request.

KoraCLI has three modes and two BASIC dialects, and they do not all follow the
same rules. Where a mode behaves differently from the others, this page says
so in a **Differences** note.

## The screen

*Works (step 1b),* on a screen of KoraCLI's own: the top bar says
`KoraCLI`, the bottom bar the mode (`PY` or `OS`) and the function keys
there are so far (F1 Help, F2 Mode). Meta+M goes back to the shell's screen.

```
┌──────────────────────────────────────────────────────────────┐
│ KoraCLI                                                      │  top bar
├──────────────────────────────────────────────────────────────┤
│ >>> msg = "hello world"                                      │
│ >>> 10 PRINT msg$                                            │  text
│ >>> %RUN                                                     │
│ hello world                                                  │
│ >>> _                                                        │
├──────────────────────────────────────────────────────────────┤
│ PY | F1 Help  F2 Mode  F3 List  F5 Run  F6 Save  F7 Load     │  bottom bar
└──────────────────────────────────────────────────────────────┘
```

- The **top bar** shows the title.
- The **bottom bar** shows the current mode on the left (`PY`, `BAS` or
  `BBC` for BASIC mode in each dialect, `OS`) and the function keys that
  mean something in that mode.
- Nothing a program prints can overwrite the bars, not even a clear screen.
- Shift+PgUp and Shift+PgDn scroll back through the last 1000 lines.

KoraCLI runs on a screen of its own. Meta+M (Command+M on a Mac keyboard,
Win+M on a PC keyboard) switches between screens; when a program that opened
its own screen ends, you are back in KoraCLI.

## Modes

*Planned: Python and OS modes in phase 1, BASIC mode in phase 2.*

F2 switches mode: Python, then BASIC, then OS, then Python again.

| Mode | Indicator | Prompt | An unprefixed line is |
|---|---|---|---|
| Python | `PY` | `>>>`, and `...` inside a block | Python |
| BASIC, Classic dialect | `BAS` | `READY.` after each command, no prefix on the input line | Classic BASIC |
| BASIC, BBC dialect | `BBC` | `>` | BBC BASIC |
| OS | `OS` | `$` | an OS command |

**Differences:** Classic BASIC's prompt is a status line, printed when a
command finishes, as on a C64; you type on the line below it. The other modes
put their prompt in front of the line you type.

### Prefixes in Python and BASIC modes

In Python mode and BASIC mode, the first character of the line you type at
the prompt decides where it goes:

| The line starts with | It goes to | Example |
|---|---|---|
| A digit | The BASIC program, as a program line (stored, not run) | `10 PRINT "HELLO"` |
| `!` | An OS command | `!dir` |
| `%` | The other language, for this line only | `%RUN` in Python mode, `%print(x)` in BASIC mode |
| Anything else | The current mode's language | `print("hi")`, `PRINT "HI"` |

Things to know:

- **A line that starts with a number is always a BASIC program line,** even
  in Python mode. It is stored without being checked; mistakes show when the
  program runs. A Python expression that starts with a number goes in
  parentheses: `(10 in s)`.
- **A number on its own deletes that program line:** `10`.
- **The prefixes count only on the line typed at the prompt.** Lines typed
  at `...` inside a Python block are Python, so a line such as `1, 2]` that
  closes a list is not a program line, and `%` means nothing there.
- **The exception is `!`:** inside a block it still works where a statement
  starts:
  ```
  >>> for f in files:
  ...     !cat {f}
  ```
- **OS mode has no prefixes at all.** Every line is an OS command; to type
  Python or BASIC, switch mode with F2.

## Python mode

*Works (step 1b),* with the editing below. Program lines are stored (BASIC
comes in phase 2), and `!` says OS commands come in step 1c.

KoraCLI's Python is MicroPython. Your code runs in a namespace of its own,
with `kora` there already; the expressions you type have their value
printed, as at Python's own prompt. ESC stops running code with
`KeyboardInterrupt`, and so does Ctrl+C; `input()` works as usual.

The same MicroPython also runs on its own: type `micropython` in the shell
for MicroPython's own REPL, on the shell's console, or `micropython
script.py args` to run a script (steps 0a and 0b). What it has, in KoraCLI
too:

- MicroPython v1.29.0's core language, with integers of any size, floats
  (double precision; `repr` gives the shortest form that reads back almost
  always, not always) and Unicode strings. Tracebacks give line numbers.
- MicroPython's line editor: Left, Right, Home, End, Backspace, Delete, Up
  and Down for the history, Tab to complete a name, and an indented block
  continued after `...` (auto-indent).
- Ctrl+D on an empty line ends it. Ctrl+E is paste mode, Ctrl+A and Ctrl+B
  the raw REPL and back (for tools).
- **Ctrl+C** raises `KeyboardInterrupt` in running code, `time.sleep` and
  `input()` included; at the prompt it drops the line. Code stuck where
  Python cannot see the break (a long calculation inside one call, such as
  `2 ** 100000000`) ends at a second Ctrl+C, as any program does.
- **Files:** `open(path, mode)` with `r`, `w`, `a`, `x`, `b` and `+`; read,
  readline, readlines, write, seek, tell, iteration by line, `with`. Paths
  are KoraOS's: relative, or with a volume or an assign (`sys:python/x.py`,
  `df0:notes.txt`).
- **Modules:** `import` finds `.py` files in `sys.path`: the script's
  directory (the current one at the REPL), then `sys:python`. Built in:
  `sys` (`argv`, `path`, `exit`), `math`, `gc`, `time` (`sleep`,
  `sleep_ms`, `sleep_us`, `ticks_ms`, `ticks_us`, `ticks_diff`, `ticks_add`;
  no wall clock yet), `os` and `kora`.
- **`os`:** `listdir`, `stat` (the type and the size; times are 0), `mkdir`,
  `rmdir`, `remove`, `rename`, `getcwd`, `chdir` and `sync`. A failure is
  `OSError` with the most likely error number: KoraOS does not say why a
  call failed yet.
- **`kora`:** `uptime()` (seconds since boot), `volumes()` (a `(device,
  label, boot, read_only)` tuple each) and `assigns()` (a `(name, target)`
  tuple each). Running commands comes with OS mode.
- The heap grows as a program needs it and shrinks again. A script's exit
  code is 0, 1 for an exception, 130 for an uncaught `KeyboardInterrupt`, or
  what `sys.exit()` gave.

- An indented block continues on the next line with the `...` prompt.
- One blank line inside a block stays part of it, so a function can contain
  blank lines.
- Two blank lines in a row end the block and run it. Ctrl+Enter runs it at
  once.
- PgUp and PgDn go back and forward through history a whole block at a time.
- ESC interrupts running Python code with `KeyboardInterrupt`.

**Differences from desktop Python:** the prompt accepts the prefixes above,
which are not Python. They work only at the prompt: `.py` files are plain
Python, and in them you call `kora.run` and `kora.capture` instead of `!`.

## OS commands

*Works (step 1c),* in Python mode and in code, as below. What is not there
yet: `kill` and the other built-ins (step 1d), the startup file that loads
wrappers (phase 6), and job objects: `job = !cmd &` gives the job's number
for now (phase 7).

### Running and capturing

| You type | What happens |
|---|---|
| `!cmd args` | The command runs, its output appears on the screen, and KoraCLI waits for it to end. Nothing more is printed; the exit status is in `kora.status`. |
| `out = !cmd args` | The output is captured into `out` instead. |

A captured result is a string with extras: `out.status` (the exit code),
`out.lines` (the output split into lines) and `out.stderr` (the error
output). Commands that have a Python wrapper may return richer objects:
`files = !dir` gives file objects, so `files[0].name` works.

A program that needs the whole display opens a screen of its own and you come
back to KoraCLI when it ends.

### Finding the command

1. A **wrapper** in `kora.cmd` (`!dir` runs `kora.cmd.dir`). A Python
   variable called `dir` never hides it.
2. A **program in `c:`**.
3. An **explicit path** (`!/bin/randombin`) runs that program directly,
   skipping wrappers.

### Arguments

- Bare words are strings; quote an argument that contains spaces.
- `{expr}` puts a Python value in the command line:
  - a list or tuple gives one argument per element: `!cat {files}`;
  - any other value gives one argument, never split, even if it contains
    spaces;
  - inside a word the value joins it: `file{n}.txt` is one argument;
  - `{{` and `}}` are literal braces.
- `< in.txt`, `> out.txt` and `>> out.txt` redirect input and output to
  files.

**Differences:** `{expr}` works in Python and BASIC modes only, not in OS
mode.

### Background jobs and cores

These words at the very end of the line change how the program runs:

| Suffix | Meaning | Example |
|---|---|---|
| `&` | Run in the background; the prompt comes back at once | `!render scene.k &` |
| `@N` | Run on CPU core N | `!render scene.k @2` |

They can be combined in either order (`@2 &`). Only a word of its own counts,
so `bob@example.com` is an ordinary argument. A program stays on the core it
started on. `kill` ends a background job.

KoraCLI prints the job's number when it starts (`[12]`), and `[12] exited
with 0` at the next prompt after it ends. A background job's output appears
on the screen as it comes, even in the middle of a line you are typing.

**Today:** Ctrl+C while a program runs in front is passed on to it, which
ends it unless it handles breaks; ESC is the program's own key. Keys typed
while a program runs, and not read by it, reach the prompt afterwards.

### In Python code

`!` exists only at the prompt. In `.py` files, and in your own wrappers:

| At the prompt | In code |
|---|---|
| `!cmd a b` | `kora.run("cmd", "a", "b")`, which returns the exit status |
| `out = !cmd a b` | `out = kora.capture("cmd", "a", "b")` |
| `!cmd &` and `!cmd @2` | `background=True`, `core=2` |
| `< in.txt`, `> out.txt`, `>> out.txt` | `stdin="in.txt"`, `stdout="out.txt"`, `append=True` |

### Wrappers: new commands in Python

A wrapper is a Python function registered as a command:

```python
@kora.cmd.register
def randombin(*args):
    return parse(kora.capture("/bin/randombin", *args))
```

Calling the program by its full path keeps the wrapper from calling itself.
At the prompt, `!randombin` shows what the wrapper returns (unless that is
`None`); `x = !randombin` keeps it. `kora.cmd.names()` lists the wrappers,
and `kora.cmd.unregister("name")` takes one away.
Wrappers kept in your folder are loaded by the startup file at every start.
They are KoraCLI's only extension mechanism: there is no separate shell
scripting language.

## OS mode

*Begun (step 1c):* F2 switches to it, with the `$` prompt, and every line
runs as a command, read as `!` reads one but without `{expr}`. The
built-ins (`cd` and the rest) and a volume name alone come with step 1d.

A plain command shell with the `$` prompt: every line is an OS command, found
and run as with `!` above, with the same `&`, `@N` and redirection.

- `cd`, `pwd`, `volumes`, `sync`, `assign`, `kill`, `help` and `exit` are
  built in.
- A volume or assign name on its own (`dh0:`) changes directory.

**Differences:** no prefixes, no line numbers and no `{expr}`. OS mode is not
for scripting: use Python mode for that.

## BASIC

*Planned: Classic BASIC in phase 2, BBC BASIC in phase 3.*

BASIC mode uses one of two dialects at a time, chosen in the configuration.
Switching dialect with a program in memory asks first, then clears the
program.

| | Classic BASIC | BBC BASIC |
|---|---|---|
| Modelled on | Commodore 64, MSX, GW-BASIC | BBC BASIC V (Acorn Archimedes) |
| Prompt | `READY.`, after each command | `>`, before the input line |
| Keywords and variable names | Not case-sensitive | Case-sensitive |
| Variable names | The whole name counts | The whole name counts |
| Hex numbers | `&HFF` | `&FF` |
| Calling Python | `SYS "name", args` | `SYS "name", args`, or `PROCname` / `FNname` when the program does not define them |
| Errors | `?SYNTAX ERROR IN 10` | `Mistake at line 10` |

**Differences from the originals:** both dialects follow their models'
syntax and keywords but are not exact copies. On a C64 only the first two
characters of a variable name count (`SCORE` and `SC` are the same
variable); in KoraCLI the whole name counts.

### Commands

| Command | What it does |
|---|---|
| `RUN` | Clears the variables BASIC defined (Python's stay) and runs the program |
| `LIST [range]` | Lists the program, e.g. `LIST 10-50` |
| `NEW` | Clears the program and BASIC's variables |
| `CONT` | Continues after ESC or `STOP` |
| `SAVE "name"`, `LOAD "name"` | Saves and loads `.BAS` files |
| `RENUM [start[,step]]` | Renumbers the program |
| `AUTO [start[,step]]` | Numbers lines for you as you type |
| `HELP [keyword]` | Help on a keyword |

Any other statement typed without a line number runs at once:
`PRINT 2+2`.

ESC stops a running program; `CONT` carries on from there.

### Files

Programs are saved as plain text with the `.BAS` extension. `SAVE` writes
line 0 to record the dialect (`0 REM !KoraBasic:Classic` or
`0 REM !KoraBasic:BBC`). `LOAD` reads it and offers to switch dialect if
needed. `RENUM` never moves line 0. A file without line 0 loads in the
current dialect.

## Variables shared by Python and BASIC

*Planned (phase 2).*

Python and BASIC see the same variables. BASIC's type suffix is dropped in
Python:

| BASIC | Python | Example |
|---|---|---|
| `name$` | `str` | `msg$` is `msg` |
| `name%` | `int` | `n%` is `n` |
| `name` | `float` | `x` is `x` |
| `name(...)`, from `DIM` | `list` | `DIM a(10)` is `a` |

```
>>> msg = "hello world"
>>> 10 PRINT msg$
>>> %RUN
hello world
```

The rules, and where they surprise:

- **Numbers convert.** A plain BASIC variable reading a Python `int` gets a
  float. `n%` reading a Python float drops the fraction, as `A%=1.5` does in
  BASIC.
- **Other types do not.** A string where a number is expected, or the other
  way round, is a "Type mismatch". So is any other Python value, except a
  list read as an array.
- **True and false:** Python's `True` reads as BASIC's `TRUE`, which is -1,
  and `False` as 0.
- **`A`, `A%`, `A$` and `A()` stay separate in BASIC,** as on the C64, so
  `A=5: A$="X": PRINT A` prints 5. Python has only one `A`: the value
  written last by either language (here `"X"`). When Python changes `A`,
  BASIC reads the new value next time.
- **Case:** names keep their case in Python. In Classic BASIC, where case
  does not matter, a variable keeps the spelling it was first written with
  (`Msg$`, later `MSG$`, is Python `Msg`). Reading a name it has not defined,
  Classic BASIC tries Python's exact spelling first, then any spelling; two
  different spellings in Python are an error. In BBC BASIC names are
  case-sensitive and match exactly.
- BASIC's system variables (BBC's `@%`, `TIME`) are not shared.

## Keys

| Key | Where | What it does |
|---|---|---|
| F1 | All modes | Help |
| F2 | All modes | Next mode |
| F3, F5, F6, F7 | Python and BASIC modes | `LIST`, `RUN`, `SAVE`, `LOAD` |
| ESC | While Python or BASIC runs | Interrupts it (`CONT` resumes BASIC) |
| ESC | At the prompt | Nothing yet; may clear the line |
| Ctrl+C | While a program runs | Stops the program in front. A program may catch it; a second Ctrl+C stops it anyway |
| PgUp, PgDn | At the prompt | History, a block at a time |
| Shift+PgUp, Shift+PgDn | Anywhere | Scroll back through the screen |
| Ctrl+Enter | While typing a block | Runs the whole block |
| Meta+M | Anywhere | Next screen (handled by the screen manager, never by KoraCLI) |

**Differences:** ESC stops Python and BASIC code but not programs, because
full-screen programs such as editors and games need ESC for themselves; stop
those with Ctrl+C.

**Today (step 1b):** F1 (help), F2 (between Python and OS mode), ESC and
Ctrl+C on running code, PgUp and PgDn, Ctrl+Enter, Shift+PgUp and
Shift+PgDn and Meta+M work. F3 to F7 come with BASIC. Every console sends F1
to F12 as xterm does, and a program can read each key as an event and ask
for ESC as a break (step 1a).

## Editing

*The line editor works (step 1b); the full-screen editor comes in phase 6.*

Today's line editor: Left and Right, Home and End (Ctrl+A, Ctrl+E),
Backspace and Delete, Ctrl+K and Ctrl+U, Tab (spaces to the next multiple
of four) across the lines of a block; Up and Down move between them and,
past the first or last, through the history, as PgUp and PgDn do a block at
a time. A compound statement (`def`, `for`, `if`, ...) or an open bracket or
triple-quoted string goes on at `...`, indented for you; in a block, one
blank line is kept and the second ends it. Ctrl+D on an empty prompt ends
KoraCLI. The editor is `sys:koracli/editor.py`.

Two editor styles, chosen in the configuration:

- **Line editor** (the default until the full-screen editor exists): you
  edit the line or block at the prompt; the text above it is output only.
- **Full-screen editor,** as on a C64: move the cursor anywhere and press
  Enter on any line on the screen to enter it again. Changing a listed BASIC
  line and pressing Enter replaces it. Inside a Python block being edited,
  Enter adds a line to the block.

The editor is written in Python and you can change it. If it ever fails,
KoraCLI falls back to a plain line editor, so there is always a prompt.
*Works (step 1b):* KoraCLI's Python side is in `sys:koracli` (`main.py`,
`shell.py`, `editor.py`, `dispatch.py`, `keys.py`); if it raises an
exception, MicroPython's own REPL takes over (Ctrl+D ends it).

## Graphics and sound

*Planned (phase 5).*

The same graphics and sound functions exist in both languages: `CLS`,
`MODE`, `PLOT`, `DRAW`, `CIRCLE`, `SOUND` and the rest in BASIC, spelled as
each dialect spells them, and `cls()`, `mode()`, `plot()` in Python.
Drawing happens on a screen of its own, which comes to the front while a
program draws; KoraCLI's own screen comes back when the program ends, or
with Meta+M.

## Startup and configuration

*Planned (phase 6).*

A Python startup file runs when KoraCLI starts, like `AUTOEXEC.BAT`. The
configuration sets at least the BASIC dialect, the starting mode, the editor
style, the function keys, the screen mode and font, the scrollback size and
Classic BASIC's prompt.

## Help

*Planned (phase 6).*

| Where | How |
|---|---|
| Python | `help(plot)` or `plot?` |
| BASIC | `HELP PLOT` |
| Later | F1 on a word opens help on it beside the text |

Help for a BASIC keyword and for the Python function behind it is the same
text.
