# KoraCLI user reference

KoraCLI is the shell KoraOS starts in: a fullscreen prompt where you can type
Python, a line of BASIC or an OS command, and see what happens.

> **Status: KoraCLI is the shell (phase 1 done), and Classic BASIC runs,
> sharing its variables with Python, with SAVE, LOAD, RENUM and AUTO
> (phase 2).** KoraOS starts in it, with Python, BASIC and OS modes, OS
> commands from Python, and its own editor (`koracli` starts another one).
> This reference is written ahead of the implementation and grows
> with it: each section says whether what it describes works today or which
> step brings it. The
> design behind it is `project_doc/koracli-spec.md`, and the steps are in
> `project_doc/koracli-plan.md`. Every change to KoraCLI updates this page in
> the same pull request.

KoraCLI has three modes and two BASIC dialects, and they do not all follow the
same rules. Where a mode behaves differently from the others, this page says
so in a **Differences** note.

## The screen

*Works (steps 1b, 1d and 2a),* on a screen of KoraCLI's own, the one KoraOS
starts in: the top bar says `KoraCLI`, the bottom bar the mode (`PY`, `BAS`
or `OS`) and the function keys there are so far (F1 Help, F2 OS or Back,
F3 PY, F4 BAS). Meta+M
switches between it and other programs' screens.

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
│ PY | F1 Help  F2 OS  F3 PY  F4 BAS  F5 Run  F6 Save  F7 Load │  bottom bar
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

*Works: Python and OS modes (phase 1), BASIC mode in the Classic dialect
(phase 2) and in the BBC dialect (from step 3b).*

F3 switches to Python mode and F4 to BASIC mode. F3 does nothing in Python
mode; F4 in BASIC mode switches dialect (see [BASIC](#basic)). F2 goes to OS mode, and from there back to the language mode you
came from: you choose a language and keep it, and visit OS mode.

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
  Python or BASIC, switch mode (F2 back, F3, F4).

## Python mode

*Works (steps 1b and 1c),* with the editing below.

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

*Works (steps 1c and 1d),* in Python mode and in code, as below; the old
shell's built-ins (`cd`, `kill`, ...) are wrappers, so `!cd docs` works
too. What is not there yet: the startup file that loads wrappers (phase 6),
and job objects: `job = !cmd &` gives the job's number for now (phase 7).

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

KoraCLI prints the job's number when it starts (`[12] started`), and `[12]
done, exit 0` at the next prompt after it ends. A background job's output appears
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

*Works (step 1d).* F2 switches to it from Python or BASIC mode, with the `$` prompt, and every line
runs as a command, read as `!` reads one but without `{expr}`; a backslash
takes the next character as it is (`my\ file.txt`), as quotes do. As the
shell before KoraCLI did, each command's end is reported (`[12] exited with
0`), a background job's start and end too (`[13] started`, `[13] done,
exit 0`), and Ctrl+C at the prompt drops the line (`^C`). KoraCLI starts in
Python mode: F2 once for OS mode, F2 again to go back.

A plain command shell with the `$` prompt: every line is an OS command, found
and run as with `!` above, with the same `&`, `@N` and redirection.

- `cd`, `pwd`, `volumes`, `sync`, `assign`, `kill`, `help` and `exit` are
  built in.
- A volume or assign name on its own (`dh0:`) changes directory.

**Differences:** no prefixes, no line numbers and no `{expr}`. OS mode is not
for scripting: use Python mode for that.

## BASIC

*Works: Classic BASIC (phase 2) and BBC BASIC (phase 3).*

BASIC mode uses one of two dialects at a time, Classic BASIC when KoraCLI
starts. F4 pressed in BASIC mode switches dialect while no program is in
memory, saying `BBC BASIC` or `Classic BASIC`; with a program in memory, it
stays and says to `NEW` or `SAVE` first. `LOAD` (or BBC BASIC's `CHAIN`)
of a file saved in the other dialect asks `Switch to BBC BASIC? (y/N)` (or
Classic BASIC): yes clears the program in memory, switches, and loads the
file (and runs it, for `CHAIN`); anything else leaves things as they were.

| | Classic BASIC | BBC BASIC |
|---|---|---|
| Modelled on | Commodore 64, with GW-BASIC's and MSX's additions | BBC BASIC V (Acorn Archimedes) |
| Prompt | `READY.`, after each command | `>`, before the input line |
| Keywords | Any case | Capitals only, so a word in small letters is always a name; abbreviations such as `P.` (PRINT) are stored in full |
| Case of names | Not case-sensitive | Case-sensitive |
| Variable names | The whole name counts | The whole name counts |
| A variable not yet set | 0 or `""` | `No such variable`, except `A%` to `Z%` (always there, kept by `RUN` and `CLEAR`) |
| Hex numbers | `&HFF` | `&FF` |
| Calling Python | `SYS "name", args` | `SYS "name", args`, or `PROCname` / `FNname` when the program does not define them |
| Errors | `?SYNTAX ERROR IN 10` | `Mistake at line 10`, with BBC BASIC V's `ERR` numbers |
| Numbers printed | A sign or a space before, a space after | Right-aligned in fields of ten, until a `;` |
| OS commands | `!` | `!`, or `*` as a star command (`*CAT`, `*DIR`), and `OSCLI` for one built as a string |
| Numbers | 9 digits | Reals are doubles (a BBC's have 5 bytes), so the last digits may differ; `%` integers are 32 bits |

**Differences from the originals:** both dialects follow their models'
syntax and keywords but are not exact copies. What a BBC BASIC manual says
applies, though old programs are not promised to run unchanged. On a C64 only the first two
characters of a variable name count (`SCORE` and `SC` are the same
variable); in KoraCLI the whole name counts.

### Commands

| Command | What it does | Status |
|---|---|---|
| `RUN [line]` | Clears BASIC's variables and runs the program (from `line`) | Works |
| `LIST [range]` | Lists the program: `LIST 10-50`, `LIST -50`, `LIST 10-`, `LIST 10` | Works |
| `NEW` | Clears the program and BASIC's variables | Works |
| `CONT` | Continues after ESC or `STOP`, unless the program was changed | Works |
| `CLR` | Clears the variables, keeping the program | Works |
| `SAVE "name"`, `LOAD "name"` | Saves and loads `.BAS` files (see [Files](#files)) | Works |
| `RENUM [start[,step]]` | Renumbers the program, 10 and 10 by default | Works |
| `AUTO [start[,step]]` | Numbers lines for you as you type, 10 and 10 by default | Works |
| `HELP [keyword]` | Help on a keyword | Phase 6 |

Any other statement typed without a line number runs at once:
`PRINT 2+2`. A `GOTO` typed that way runs the program from that line,
keeping its variables.

ESC stops a running program with `BREAK IN 20`; `CONT` carries on from
there. Ctrl+C does the same.

In Python mode, `%` runs a line of BASIC (`%RUN`, `%LIST`), without the
`READY.`; in BASIC mode, `%` runs a line of Python (`%print(x)`), and `!`
an OS command, as in Python mode.

### Classic BASIC

Statements:

| | |
|---|---|
| Output and input | `PRINT` (or `?`) with `;`, `,` (zones of 10 columns), `TAB(n)` and `SPC(n)`; `INPUT ["prompt";] a, b$`; `GET a$` (a key, or `""` when none is waiting); `CLS` |
| Variables | `LET` (optional), `DIM a(10, 5)`, `SWAP a, b`, `CLR`; `DEF FNname(x) = expression` |
| Flow | `GOTO`, `GOSUB` and `RETURN`, `ON n GOTO` / `ON n GOSUB`, `IF ... THEN ... ELSE ...` (THEN or ELSE may be a line number), `FOR ... TO ... STEP` and `NEXT [i, j]`, `WHILE` and `WEND`, `END`, `STOP` |
| Data | `DATA`, `READ`, `RESTORE [line]` |
| Python | `SYS "name", args` (see Calling Python from BASIC) |
| Others | `REM` or `'`, `RANDOMIZE [n]` |

Functions: `ABS`, `ATN`, `COS`, `EXP`, `INT`, `LOG`, `RND`, `SGN`, `SIN`,
`SQR`, `TAN`; `ASC`, `CHR$`, `HEX$`, `INSTR([start,] s$, t$)`, `LEFT$`,
`LEN`, `MID$`, `RIGHT$`, `SPACE$`, `STR$`, `STRING$(n, c)`, `VAL`; `FRE`,
`POS`, `INKEY$`; and the clock `TI` (sixtieths of a second) and `TI$`
(`"HHMMSS"`, which can be set).

Operators, from the tightest: `^`; unary `-`; `*` and `/`; `\` (integer
division); `MOD`; `+` and `-` (`+` joins strings); `=`, `<>`, `<`, `>`,
`<=`, `>=`; `NOT`; `AND`; `OR` and `XOR`. A comparison is -1 when true and
0 when false, and `AND`, `OR` and `NOT` work on the bits of whole numbers.

Variables: `name` holds a number, `name%` a whole number, `name$` a string,
and they are different variables (`A`, `A%`, `A$`); a variable not yet set
is 0 or `""`. An array used without `DIM` has indices 0 to 10.

Keywords need no spaces where only a keyword can stand: `FORI=1TO10:PRINTI`
works. Elsewhere a word is a name, so a name may contain or start with a
keyword (`TOTAL`, `FORMAT`, `NOTE`), except that a name cannot start with
`FN` (a function call), nor with `REM` or `DATA` at a statement's start.

Numbers print as a C64 prints them: a space or a minus sign before, a space
after, up to 9 digits, no 0 before the point (`.5`), and E notation below
.01 and from 1E9 (`1E-03`, `1.23456789E+09`). A program's mistakes show
when the line runs: `?SYNTAX ERROR IN 10`.

**Differences from the C64:**
- The whole name counts, and keywords inside names are left alone (above).
- From GW-BASIC and MSX: `ELSE`, `WHILE`/`WEND`, `'` remarks, `INKEY$`,
  `&H` hex numbers, `HEX$`, `INSTR`, `SPACE$`, `STRING$`, `SWAP`, `MOD`,
  `\`, `XOR`, `CLS` and `RANDOMIZE`.
- Numbers are 64-bit floating point (16 digits inside, 9 printed), and
  whole numbers (`%`, `AND`, `OR`, `NOT`) 32 bits, not 16. Strings have no
  length limit, and `CHR$` takes any Unicode character.
- `GET` gives the C64's codes for Return (13), Delete (20), Home (19) and
  the cursor keys (17 down, 145 up, 29 right, 157 left); other keys give
  their character.
- `SYS "name"` calls Python; `SYS` with an address, `PEEK`, `POKE` and
  `WAIT` come in phase 4. Not there: the C64's screen codes in strings
  (colours, cursor moves), and `ST`.

### BBC BASIC

*Works (steps 3b to 3e): the statements, the constructs over several lines,
procedures and functions, error handling, star commands and files below.*

BBC-inspired, modelled on BBC BASIC V: what a BBC BASIC manual says
applies, though old programs are not promised to run unchanged.

Statements:

| | |
|---|---|
| Output and input | `PRINT` with `;`, `,` (fields of `@%`'s width, 10), `'` (a new line), `~` (the next number in hex), `TAB(x)`, `TAB(x, y)` and `SPC(n)`; `INPUT ["prompt"[,]] a, b$`, `INPUT LINE a$`; `CLS` |
| Variables | `LET` (optional), `+=` and `-=`, `DIM a(10, 5)`, `SWAP a, b`, `CLEAR`, `TIME = n` |
| Flow | `GOTO` and `GOSUB` (to a line number or an expression), `RETURN`, `ON n GOTO` / `ON n GOSUB` with `ELSE`, `IF ... [THEN] ... ELSE ...` on one line (THEN or ELSE may be a line number), `FOR ... TO ... STEP` and `NEXT [i, j]`, `END`, `STOP` |
| Over several lines | `IF ... THEN` ending its line, then `ELSE` and `ENDIF`; `CASE ... OF` ending its line, then `WHEN a, b:`, `OTHERWISE` and `ENDCASE`; `REPEAT` ... `UNTIL`; `WHILE` ... `ENDWHILE` |
| Procedures and functions | `DEF PROCname(a, b$)` ... `ENDPROC`, called as `PROCname(1, "x")`; `DEF FNname(x)` ... `= value`, or `DEF FNname(x) = expression` on one line; `LOCAL a, b$`; `RETURN` before a parameter (`DEF PROCswap(RETURN a, RETURN b)`) |
| Errors | `ON ERROR statements`, `ON ERROR LOCAL statements`, `ON ERROR OFF`; `LOCAL ERROR`, `RESTORE ERROR`; `ERROR number, "message"`; `REPORT` |
| Data | `DATA` (to the line's end), `READ`, `RESTORE [line]` |
| OS commands | `*command` as a line's first statement (`*CAT`), `OSCLI "command"` for one built as a string |
| Others | `REM` |

Commands: `SAVE "name"`, `LOAD "name"` and `CHAIN "name"` (load, then
run; see [Files](#files)), `RENUMBER [start[,step]]` and `AUTO
[start[,step]]` (as Classic BASIC's RENUM and AUTO), `LISTO n` (1, the
default, a space after the line number; 2 the constructs' insides
indented; 3 both), `RUN`, `LIST` (`LIST 20`, `LIST 20,50`, `LIST 20,`, `LIST ,50`),
`NEW`, and `CONT`, which BBC BASIC does not have.

Functions: `ABS`, `ACS`, `ASN`, `ATN`, `COS`, `DEG`, `EXP`, `INT`, `LN`,
`LOG` (base 10), `RAD`, `SGN`, `SIN`, `SQR`, `TAN`, `PI`, `RND`, `RND(n)`;
`ASC`, `CHR$`, `INSTR(s$, t$[, start])`, `LEFT$(s$[, n])`, `LEN`,
`MID$(s$, start[, n])`, `RIGHT$(s$[, n])`, `STR$`, `STR$~`,
`STRING$(n, s$)`, `VAL`, `EVAL`; `TRUE`, `FALSE`; `GET`, `GET$` (they wait
for a key), `INKEY(t)` and `INKEY$(t)` (wait up to `t` hundredths of a
second; -1 or `""` when none came), `POS`, `COUNT`, `TIME` (hundredths of a
second), `ERR`, `ERL`, `REPORT$`. A function of one argument needs no
brackets: `SIN X`, `LEN A$`.

Operators, from the tightest: unary `-`, `+`, `NOT` and the functions;
`^`; `*`, `/`, `DIV`, `MOD`; `+` and `-` (`+` joins strings); `=`, `<>`,
`<`, `>`, `<=`, `>=`, `<<`, `>>`, `>>>`; `AND`; `OR`, `EOR`. So `NOT A = B`
is `(NOT A) = B` and `-2^2` is 4, as on a BBC. A comparison is -1 when
true and 0 when false.

Numbers: `&FF` is hex and `%1010` binary. Whole numbers that fit in 32 bits
are integers (`%` variables, `7 DIV 2`, `1 + 2`); `+` and `-` on integers
wrap at 32 bits, `*` gives a real when the result does not fit, and `/` and
`^` always give reals. Reals print in `@%`'s format, nine digits by
default, with an exponent from 1E9 and below 0.1 (`1.23456789E9`, `5E-2`).

The constructs over several lines nest, and are matched when they run:
an `IF ... THEN` whose condition is false goes on after its `ELSE` or
`ENDIF`, a `CASE` after the first `WHEN` with an equal value (or
`OTHERWISE`, or `ENDCASE`), and a `WHILE` whose condition is false after
its `ENDWHILE`, which is why `WHILE` may run its body no times and
`REPEAT` always runs it once. They may be on one line too
(`REPEAT X%+=1: UNTIL X%>4`), and a statement may follow `REPEAT`,
`WHEN`, `OTHERWISE` or `ELSE` without a colon (`REPEAT UNTIL GET`). A
construct left open is `Missing ENDIF`, `Missing ENDCASE` or `Missing
ENDWHILE`.

```
10 FOR N%=1 TO 3
20   CASE N% OF
30   WHEN 1: PRINT "one"
40   WHEN 2
50     IF TRUE THEN
60       PRINT "two"
70     ENDIF
80   OTHERWISE PRINT "more"
90   ENDCASE
100 NEXT
```

A procedure or function is found by its `DEF`, the first statement of a
line, wherever it is; reached as the program runs, a `DEF` line is skipped,
so the definitions usually follow an `END`. Parameters and `LOCAL`
variables are the call's own: what they held before comes back when it
ends, in BASIC and in Python's namespace, and a `RETURN` parameter's value
goes back into the variable passed. Loops left open inside a call end with
it. Functions may call themselves (`DEF FNfact(n)`, `IF n <= 1 THEN = 1`,
`= n * FNfact(n - 1)`). A `PROC` or `FN` the program does not define calls
Python's function of that name (see [Calling Python from
BASIC](#calling-python-from-basic)).

```
10 PRINT FNfact(10)
20 PROCgreet("Ada")
30 END
40 DEF FNfact(n)
50 IF n <= 1 THEN = 1
60 = n * FNfact(n - 1)
70 DEF PROCgreet(n$)
80   LOCAL t$: t$ = "Hello, " + n$
90   PRINT t$
100 ENDPROC
```

`ON ERROR` sets the rest of its line as the handler: when an error comes,
`ERR`, `ERL` and `REPORT$` tell which, where and what, and the handler runs,
forgetting the loops, subroutines and calls that were running. `ON ERROR
LOCAL` keeps them, and inside a procedure `LOCAL ERROR` first keeps the
handler in use, to come back when the procedure ends. ESC is an error too
(`ERR` 17), so a handler can catch it; ESC pressed again within a second
stops the program anyway. A handler lasts while its program runs.

Keywords are capitals only: `print` is a variable. Where only a keyword can
stand they need no spaces (`FORI=1TO10:PRINTI`), as in Classic BASIC.
Abbreviations are written in full when the line is stored: `P."HI":G.10`
lists as `PRINT"HI":GOTO10`. Errors are BBC BASIC V's (`No such variable at
line 20`), with its numbers; ESC stops a program with `Escape at line 20`,
`STOP` with `STOP at line 20`, and `CONT` goes on.

**Differences from BBC BASIC V:**
- Reals are 64-bit doubles, not 5-byte floats, so the last digits may
  differ; strings have no length limit; `CHR$` takes any Unicode character.
- Keywords inside names are left alone where a name can stand (a BBC reads
  `PRINTER` as `PRINT ER` everywhere).
- `GET` gives a BBC's codes for Return (13), Delete (127) and the cursor
  keys (136 left, 137 right, 138 down, 139 up). `INKEY` with a negative
  number (a key scan) gives -1. `EVAL` takes numeric expressions only.
- `CONT` is KoraCLI's; it cannot go back into a `PROC` or `FN` that a
  break or `STOP` left. A name starting with `PROC` or `FN` is a call.
  Parameters are variables, not arrays. A `PROC` or `FN` the program does
  not define is Python's, not a mistake.
- Star commands are KoraOS's commands, run as OS mode runs them: `*CAT` (or
  `*.`) is `ls`, `*DIR` `cd`, `*TYPE` `cat`, `*DELETE` `rm`, `*RENAME`
  `mv`, `*COPY` `cp` and `*CDIR` `mkdir`; any other runs as typed (`*echo
  hi`). As on a BBC, `*` must start the line; mid-line, use `OSCLI`.
- Files are text, as Classic BASIC's (`.BAS`, a header line). `LOAD` in a
  program ends it with the new one in memory; `CHAIN` runs it, keeping
  only `A%` to `Z%`. A file's mistakes are `Bad program`, a missing file
  `File not found` (`ERR` 214).
- `LISTO` has KoraCLI's two options; BASIC V's others (splitting at
  colons and the like) are not there.
- Not there yet: the indirection operators and `SYS` (phase 4), and `MODE`,
  `PLOT`, `VDU` and the rest of the graphics and sound (phase 5).

### Files

*Works (step 2c), in Classic BASIC.*

```
READY.
SAVE "HELLO"
SAVING HELLO
READY.
LOAD "HELLO"
SEARCHING FOR HELLO
LOADING
READY.
```

- **Programs are plain text,** as `LIST` shows them, in the current
  directory or wherever the name says (`SAVE "df0:GAME"`). `.BAS` is added
  when the name has no extension. The name is any string expression
  (`SAVE N$`), and a C64's `,8` or `,8,1` after it is accepted and ignored.
- **The file's first line records the dialect:** `0 REM
  !KoraBasic:Classic`, written by `SAVE` (`0 REM !KoraBasic:BBC` in BBC
  BASIC). The header belongs to the file only: `LOAD` takes it away, so
  `LIST` never shows it and a program's own line 0 stays its own. A file
  without a header loads in the dialect in use; one saved in the other
  dialect asks to switch (see [BASIC](#basic)), and one naming a dialect
  KoraCLI does not have is `?UNKNOWN DIALECT ERROR`.
- **`LOAD` replaces the program and clears BASIC's variables,** as `NEW`
  does. In a program it chains, as on a C64: the new program runs from its
  start with the variables kept.
- **A file written by hand** may have blank lines and keywords in any case.
  A line without a number, or one past 63999, is `?FILE DATA ERROR`, and
  the program in memory stays as it was.
- **Errors:** `?FILE NOT FOUND ERROR`, `?MISSING FILE NAME ERROR`, and
  `?WRITE ERROR` when the file cannot be written (an SD card or USB drive
  is read-only in a normal build; the ramdisk and a writable VirtIO disk
  are not). `SAVE` syncs the volume, so a failure shows there.
- `SAVING` and `SEARCHING FOR` show when the command is typed, not in a
  program.

### RENUM and AUTO

*Works (step 2c).*

- **`RENUM [start[,step]]`** numbers the lines from `start`, `step` apart,
  and changes the line numbers that `GOTO`, `GOSUB`, `ON`, `THEN`, `ELSE`,
  `RESTORE` and `RUN` name (also `GOTO30` written without a space). Line 0
  never moves. A `GOTO` to a line that is not there stays as it is, with a
  warning: `?UNDEF'D STATEMENT 99 IN 30`. Line numbers past 63999 are
  `?ILLEGAL QUANTITY ERROR`, and nothing changes.
- **`AUTO [start[,step]]`** types each line's number for you: the prompt
  shows `10 `, and after each program line, the next. Where a line is
  there already it is shown, to change or keep. Enter on the number alone,
  Ctrl+C, or a line that is not a program line ends `AUTO`; typing another
  number goes on from there.

**Differences:** a C64 has neither; they are GW-BASIC's. GW-BASIC marks a
line already there with `*` and ends `AUTO` only with Ctrl+C; KoraCLI
shows the line, since its editor can change it.

## Variables shared by Python and BASIC

*Works (step 2b), in Classic BASIC;* BBC BASIC comes in phase 3.

Python and BASIC see the same variables: the ones in KoraCLI's Python
namespace, where your code at `>>>` runs. BASIC's type suffix is dropped in
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
  (`Msg$`, later `MSG$`, is Python `Msg`). For a name Python already has,
  Classic BASIC tries Python's exact spelling first, then any spelling, and
  reads and writes that one: with Python's `Total`, BASIC's `TOTAL = 9`
  changes `Total`. Two spellings in Python that match (`abc` and `ABC` for
  `Abc`) are `?AMBIGUOUS NAME ERROR`. A name is looked for again with each
  command you type, so a name Python defines meanwhile is found. In BBC
  BASIC names are case-sensitive and match exactly.
- **Arrays are Python lists,** nested for more than one dimension
  (`DIM M(1, 2)` is `[[0.0, 0.0, 0.0], [0.0, 0.0, 0.0]]`), and shared as they
  are: a change on either side is seen by the other. A Python list can be
  read as an array without `DIM`, and an index past its end is
  `?BAD SUBSCRIPT ERROR`.
- **`RUN`, `NEW` and `CLR` take away the variables BASIC made,** unless
  Python has changed them since. Python's own variables stay, with what
  BASIC last wrote in them.
- **A function's parameters are its own:** in `DEF FNTWICE(X) = X * 2`, `X`
  is the argument, whatever Python's `X` holds.
- BASIC's system variables (Classic's `TI` and `TI$`, BBC's `@%`, `TIME`)
  are not shared.

### Calling Python from BASIC

`SYS "name", args` calls the Python function `name` with the arguments
(numbers as floats, strings as strings); its result is dropped. Without
such a function, `name` is an OS command or a wrapper (`kora.cmd`), run with
the arguments as text, numbers as BASIC prints them: `SYS "echo", 5`. A name
that is neither is `?UNDEF'D FUNCTION ERROR`; a Python exception in the
function prints its type and message, then `?PYTHON ERROR IN 20`. `SYS` with
an address (`SYS 49152`) comes with machine access in phase 4.

In BBC BASIC, a `PROC` or `FN` the program does not define calls the
Python function of that name, with the arguments' values: `PROCled_on(17)`
calls `led_on(17)`, and `FNhalf(9)` is `half(9)`'s value. A function's value
for an `FN` must be a number (`True` is -1) or a string, else it is `Type
mismatch`; a Python exception is the error, with its type and message
(`ZeroDivisionError: divide by zero at line 20`). A name that is neither
the program's nor Python's is `No such FN/PROC`.

```
>>> def beep(n):
...     print("beep" * int(n))
...
...
>>> %SYS "beep", 3
beepbeepbeep
```

## Keys

| Key | Where | What it does |
|---|---|---|
| F1 | All modes | Help |
| F2 | Python and BASIC modes | OS mode |
| F2 | OS mode | Back to the language mode you came from |
| F3 | All modes | Python mode |
| F4 | Python and OS modes | BASIC mode |
| F4 | BASIC mode | The other dialect, while no program is in memory |
| F5, F6, F7, F8 | Python and BASIC modes | `RUN`, `SAVE`, `LOAD`, `LIST` |
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

F5 to F8 type their command at the cursor, as GW-BASIC's keys do, with `%`
in front in Python mode on an empty line: F5 `RUN` and F8 `LIST` press Enter
too, F6 types `SAVE "` with the name last saved or loaded (Enter saves it
again), and F7 types `LOAD "` for the name. In OS mode they do nothing.

**Today:** all of the keys above work. Every console sends F1
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
