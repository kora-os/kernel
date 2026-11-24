# KoraCLI: shell specification

*Status: draft v0.4, 24 November 2026. In the repository since v0.4; git holds its history.*
*"KoraCLI" is a working name.*

This document specifies the KoraOS command-line shell: the fullscreen environment the user lands in when the machine starts. It is meant as input for a development plan, so each section marks what is **decided** and what is only **proposed** (to be confirmed or changed during implementation). The plan itself, with its steps and their status, will be `koracli-plan.md`.

---

## 1. Goals

KoraOS aims to be "what a home computer would look like in 2026" (see `MANIFESTO.md`). The shell is the most visible part of that idea. It should feel like switching on a C64, an MSX or a BBC Master: you are immediately at a prompt, you can type a BASIC line or a command and see what happens.

The shell combines:

- **Python** (MicroPython) as the primary, modern language.
- **BASIC**, in two selectable dialects, for the retro experience and for beginners.
- **OS commands**, reachable from both languages, plus a plain **OS mode** that behaves as an ordinary command shell.

KoraCLI is intended to be the **only shell** of KoraOS. KoraOS supports Amiga-style **screens**, so KoraCLI runs on its own screen and can coexist with other screens (e.g. a future GUI) running at the same time.

Guiding principles:

1. **Predictable.** The user should always know which language will run a line. Prefer simple, explicit rules over clever guessing.
2. **Immediate.** No login, no setup. Type and run.
3. **Shared.** Python and BASIC share variables and the same underlying libraries (graphics, sound, GPIO).
4. **Hackable.** The shell and the BASIC interpreter are written in Python, so users can read and extend them.

---

## 2. Terminology

| Term | Meaning |
|---|---|
| **Mode** | What the prompt does with an unprefixed line: *Python mode*, *BASIC mode* or *OS mode*. |
| **Dialect** | The active BASIC variant: *Classic BASIC* or *BBC BASIC*. Only one is active at a time. |
| **Program** | The numbered BASIC listing currently in memory. |
| **Program line** | An input line starting with a line number; it is stored in the program, not executed. |
| **Immediate line** | Any input line without a line number; it is executed straight away. |
| **OS command** | A KoraOS command or program, invoked with the `!` prefix (or directly in OS mode). |
| **Wrapper** | A Python function registered in `kora.cmd` that implements or wraps an OS command. |
| **Job** | A program started from the shell, in the foreground or in the background. |
| **Screen** | An Amiga-style full display surface. KoraCLI runs on one screen. |

---

## 3. Screen layout (decided)

```
┌─────────────────────────────────────────────────────────────┐
│ KoraCLI                                                     │  ← top bar
├─────────────────────────────────────────────────────────────┤
│                                                             │
│ >>> msg = "hello world"                                     │
│ >>> 10 PRINT msg$                                           │  ← main area
│ >>> %RUN                                                    │    (text grid)
│ hello world                                                 │
│ >>> _                                                       │
│                                                             │
├─────────────────────────────────────────────────────────────┤
│ PY | F1 Help  F2 Mode  F3 List  F5 Run  F6 Save  F7 Load    │  ← bottom bar
└─────────────────────────────────────────────────────────────┘
```

### 3.1 Top bar
- Left: title, `KoraCLI`.
- Nothing else for now. The right side is reserved for future status icons (e.g. configuration, dialect selector).

### 3.2 Main area
- A fixed **text grid** (columns × rows determined by screen mode and font).
- Graphics start on a canvas screen of their own (§10.1); later a graphics layer may share this screen (§10.2).
- Scrollback: *proposed*, see Open Questions.

### 3.3 Bottom bar
- **Left:** mode indicator, VIM-style: `PY`, `BAS` or `BBC` (BASIC mode, showing the active dialect), `OS`.
- **Rest:** function-key labels. Labels change with the current mode.

### 3.4 Function keys (proposed initial map)

| Key | Python mode | BASIC mode | OS mode |
|---|---|---|---|
| F1 | Help (later: context help on keyword under cursor) | same | same |
| F2 | Next mode (PY → BASIC → OS → PY) | same | same |
| F3 | LIST | LIST | |
| F5 | RUN (the BASIC program) | RUN | |
| F6 | SAVE | SAVE | |
| F7 | LOAD | LOAD | |

F-key assignments should be configurable (see §14). Labels for keys unused in the current mode are hidden.

### 3.5 Screens and process model (decided)
*Context only: screen management and process supervision are outside the scope of KoraCLI.*

- The minimal process tree is `init → screens → shell`. If the shell is killed, it is respawned, so a KoraCLI is always available unless `init` is configured to spawn something else.
- Screens are created with the `screens` command and its syscall. A program can **create a new screen** but can **never take over** KoraCLI's screen.
- When a program's screen ends, the screen manager returns to the screen of the process that launched it, so the user lands back in KoraCLI. *(Already implemented.)*
- **Meta+M** (Command+M on Apple keyboards, Win+M on Windows keyboards) switches screens. It is owned by the screen manager, which receives keys first and forwards the rest to the foreground app. KoraCLI never sees Meta+M and must not try to bind it. *(Already implemented.)*
- Several KoraCLI instances may run on different screens. They are **independent**: each has its own Python namespace, BASIC program, mode and history. (With no memory protection, one instance could still reach into another's memory; that is left to the user's curiosity.)

### 3.6 Inside KoraCLI: two processes (decided)

KoraCLI is two processes, as the shell before it was:

- A **console task**, written in C, owns KoraCLI's screen: the text grid, the scrollback, the top and bottom bars and the console port that programs write to.
- The **interpreter**, MicroPython, is an ordinary console program on that port. It holds the Python namespace, the dispatcher (§4), BASIC (§6) and the editor (§11).

Consequences:

- The screen stays responsive while Python runs: the console task still sees ESC, scrolls back and redraws after Meta+M, because it is a task of its own.
- The bars are the console's own drawing, outside the text grid, so a program's output (even a clear screen) cannot overwrite them. The interpreter sets what they show (mode indicator, F-key labels) with a packet.
- Programs started with `!` write to the console port, never through the interpreter.
- The interpreter could run in another console too (`newcon`, a window of a later GUI), since it only needs stdin, stdout and a few packets that a console may ignore.
- The full-screen editor (§11.1) reads a row of the screen back with a packet, since text on screen may come from a program the interpreter never saw.

Rejected: one process with the console in a second thread. It would give the editor and graphics direct access to the grid and the pixels, but the grid would need a lock from day one, and the interpreter would be tied to its own screen.

---

## 4. Input dispatch (decided)

In **Python mode** and **BASIC mode**, each line typed at the prompt is routed by its **first character(s)**, in this order:

| Starts with | Goes to | Example |
|---|---|---|
| A digit | BASIC program store (program line) | `10 PRINT "HELLO"` |
| `!` | OS command | `!dir` |
| `%` | The *other* language (Python ↔ BASIC), for this line only | `%RUN` (in Python mode), `%print(x)` (in BASIC mode) |
| Anything else | The current mode's language | `print("hi")` / `PRINT "HI"` |

In **OS mode** there are **no prefixes**: every line is an OS command (see §5.4). To use Python or BASIC, the user switches mode (F2).

Rules for Python and BASIC modes:

1. **A leading number always means a BASIC program line.** The line is stored without validation; errors are reported at `RUN` time (as on the C64). This is required because BBC BASIC supports multi-line constructs such as `IF … THEN` / `ENDIF` across numbered lines, so a single line cannot be validated in isolation.
2. **Escape hatch for Python:** a Python expression that starts with a number must be wrapped in parentheses, e.g. `(10 in s)`. This is already valid Python; no new syntax is introduced.
3. A line consisting only of a number (e.g. `10`) **deletes** that program line, as in classic BASIC.
4. `!` is never valid at the start of a Python line, so it is unambiguous. (`*` was rejected as the OS prefix because `*a, b = items` is valid Python. `*` remains the native star-command prefix *inside BBC BASIC*.)
5. `%` is never valid at the start of a Python line and is not used at the start of a BASIC line.
6. **Routing applies to the line typed at `>>>` only.** Continuation lines (at `...`, inside a block or open brackets) are Python, so a line such as `1, 2]` that closes a list is never taken for a program line, and `%` is not recognised there.
7. **One exception inside blocks:** `!cmd` and `name = !cmd` (§5.2) are still translated when they start a statement after the indentation, so this works:
   ```
   >>> for f in files:
   ...     !cat {f}
   ```
   The editor knows whether a line starts a statement or continues one inside brackets, since it decides continuation (§11.2).

---

## 5. Python mode

### 5.1 Interpreter (decided)
- **MicroPython**, chosen for ease of porting.
- Prompt: the standard `>>>`, with `...` for continuation lines.
- Known limitation: MicroPython has no full `ast` module and a more limited `compile()`. The design must not depend on them.

### 5.2 OS commands (decided)
`!command args` runs an OS command. `command` is resolved in this order:

1. **Wrapper** in the `kora.cmd` namespace (e.g. `!dir` → `kora.cmd.dir`). Redefining a Python name `dir` never hides the OS command.
2. **Program on the search path** (`c:`, as in the shell before KoraCLI), if no wrapper exists.
3. An **explicit path** (`!/bin/randombin`) bypasses wrappers and runs the program directly.

#### Running vs capturing

| Form | Behaviour | Result |
|---|---|---|
| `!cmd` (not assigned) | Runs **live**: stdout/stderr go to KoraCLI's screen. A program that needs the full display opens its **own screen** (§3.5). Either way, KoraCLI waits until the program ends (unless `&` is used). | Nothing is printed; the exit status is in `kora.status` |
| `out = !cmd` | Output is **captured**. | A string subclass (see below) |

`!` exists only at the prompt. It is shorthand for two ordinary functions, which `.py` files (§12.2) and wrappers (§5.3) call instead:

| At the prompt | In code | Returns |
|---|---|---|
| `!cmd args` | `kora.run("cmd", "args")` | The exit status |
| `out = !cmd args` | `out = kora.capture("cmd", "args")` | The captured result |

Both resolve `cmd` as above and accept the job suffixes as keyword arguments *(names to be decided)*. At the prompt a bare `!cmd` prints nothing, whatever its status (most programs report their own errors); `rc = kora.run(...)` returns it.

For programs without a wrapper, the captured result is a **`str` subclass**: it behaves as an ordinary multi-line string and additionally carries:
- `.status`: exit code
- `.lines`: output split into lines
- `.stderr`: captured error output

Wrappers may return richer Python objects (e.g. `kora.cmd.dir` returns file objects); at the prompt these are pretty-printed:
```
>>> !dir
(formatted listing)
>>> files = !dir
>>> files[0].name
'README.TXT'
```

#### Arguments
- Bare words are strings (shell convention), with quoting for spaces.
- In Python and BASIC modes, Python values are interpolated with `{expr}`:
  - A list or tuple expands to **one argument per element** (`!cat {files}`).
  - Any other value becomes **one argument**, `str(value)`, never split again, even if it contains spaces.
  - Inside a word the value joins it: `file{n}.txt` is one argument. A list inside a word is an error.
  - `{{` and `}}` give literal braces, as in f-strings.
- Redirection, as in OS mode (§5.4): `!cmd < in.txt > out.txt`, with `>>` to append.

#### Job suffixes
Trailing, space-separated tokens at the **end** of the line control how the program runs:

| Suffix | Meaning | Example |
|---|---|---|
| `&` | Run in the **background**; the prompt returns immediately | `!render scene.k &` |
| `@N` | **Pin** the job to CPU core `N` | `!render scene.k @2` |

- Both may be combined, in either order: `!render scene.k @2 &`.
- Only a standalone trailing token is a suffix, so arguments such as `bob@example.com` are unaffected.
- `@N` is passed to the kernel as a parameter of the spawn syscall. Without it, the scheduler places the new process on the least busy core. Processes never migrate between cores, so the choice made at spawn time is permanent. Core placement is otherwise the scheduler's concern, not KoraCLI's.
- `job = !cmd &` returns a **job object** (status, wait, kill, captured output on completion) instead of a string.
- Job control commands: `jobs`, `fg`, `kill` *(proposed names)*.

### 5.3 Wrappers (decided)
- Users can add wrappers for any program or new command by registering a Python function, e.g.:
  ```python
  @kora.cmd.register
  def randombin(*args):
      return parse(kora.capture("/bin/randombin", *args))
  ```
  Wrappers live in `.py` files, which are pure Python, so they call `kora.run` and `kora.capture` (§5.2) rather than `!`. The explicit path keeps the wrapper from calling itself.
- Wrappers placed in a user folder are loaded by the startup file (§14), so they persist across sessions.
- Wrappers are the extension mechanism for the shell: there is no separate shell scripting language.

### 5.4 OS mode (decided)
- Prompt: `$`. Mode indicator: `OS`.
- Behaves as a **plain command shell**: every line is an OS command, resolved and run exactly as `!command` (§5.2), including the `&` and `@N` suffixes.
- **No scripting and no prefixes**: no `%`, no line numbers, no `{expr}` interpolation. Scripting is done in Python mode, and new commands are added as wrappers (§5.3).
- Shell conveniences: quoting; **redirection** to and from files (`<`, `>`, `>>`); a volume or assign name alone (`dh0:`) changes directory; wildcards *(proposed)*. Pipes are deferred; if added, passing Python objects between commands is preferred over text.
- **What the shell before KoraCLI had is kept** (`user/shell.c`, which KoraCLI replaces): programs from `c:`, quoting, `&`, `@N`, redirection and the built-ins `cd`, `pwd`, `volumes`, `sync`, `assign`, `kill`, `help` and `exit`. The built-ins are wrappers in `kora.cmd`; `cd` has to be one, since it changes the interpreter's own directory. All of it is there before `shell.c` is removed (phase 1b).

### 5.5 Paren-less function calls (proposed, deferred)
Optional REPL-only sugar so that `f 1 2` calls `f(1, 2)`. Only at the prompt, never in `.py` files. Only applied when the first token is a callable and the line is not otherwise valid Python. Not part of the MVP.

---

## 6. BASIC

### 6.1 Architecture (decided)
- The BASIC interpreter is **written in Python** and runs on MicroPython.
- It is a **Python module loaded at startup**, so it can be read, extended and replaced by users.
- Phase 1: a straightforward **interpreter** (tokenise lines, execute).
- Phase 2: **transpile** the program to Python the first time it is run and cache the result (similar to `.py` → `.pyc`), to improve speed. Invalidated whenever the program changes.
- Reference implementation worth studying: [PyBasic](https://github.com/richpl/PyBasic), which does not depend on `ast`. Check its license before reusing code.

### 6.2 Dialects (decided)
Two dialects, one active at a time, selectable from configuration:

| Dialect | Modelled on | Prompt | Notes |
|---|---|---|---|
| **Classic BASIC** | Commodore 64, MSX, GW-BASIC family | `Ok` *(proposed)* | Case-insensitive keywords and variables |
| **BBC BASIC** | BBC BASIC V (Acorn Archimedes) | `>` | Case-sensitive variables; `PROC`/`FN`; multi-line `IF`/`ENDIF`, `CASE`, `REPEAT`/`UNTIL`, `WHILE`/`ENDWHILE`; `*` star commands |

The dialects follow the syntax and core instruction set of their models, but are **not exact clones**: they are extended for a modern machine (see §9).

The name "Microsoft BASIC" is not used.

Line-number-optional BASICs (QBasic, AmigaBASIC, Turbo Basic, etc.) are **out of scope** for the shell. They may be added later as separate applications.

### 6.3 Switching dialect (decided)
- If a program is in memory, the shell **warns and asks for confirmation**.
- On confirmation, the program is cleared (implicit `NEW`) and the new dialect becomes active.

### 6.4 Immediate commands
Available in BASIC mode, or from Python mode with the `%` prefix:

| Command | Behaviour |
|---|---|
| `RUN` | Clears **BASIC-defined variables only** (Python variables are untouched), then runs the program. |
| `LIST [range]` | Lists the program (e.g. `LIST 10-50`). |
| `NEW` | Clears the program and BASIC-defined variables. |
| `CONT` | Continues after a break or `STOP`. |
| `SAVE "name"` / `LOAD "name"` | See §12. |
| `RENUM [start[,step]]` | Renumbers the program, preserving line 0 (see §12). |
| `AUTO [start[,step]]` | Automatic line numbering while typing. |
| `HELP [keyword]` | See §13. |

Any other BASIC statement typed without a line number is executed immediately (e.g. `PRINT 2+2`, `POKE …`).

---

## 7. Shared variables (decided, details partly open)

Python and BASIC share one variable space. BASIC type suffixes map to Python types:

| BASIC | Python | Example |
|---|---|---|
| `name$` | `str` | `msg$` ↔ `msg` |
| `name%` | `int` | `n%` ↔ `n` |
| `name` (no suffix) | `float` | `x` ↔ `x` |
| `name(…)` arrays (`DIM`) | `list` *(proposed)* | `DIM a(10)` ↔ `a` |

```
>>> msg = "hello world"
>>> 10 PRINT msg$
>>> %RUN
hello world
```

Case rules:
- Names keep their case when exported to Python, in **both** dialects. In Classic BASIC, `A` defines Python `A` (uppercase).
- In BBC BASIC, names are case-sensitive, so they map one-to-one.
- A type mismatch (e.g. `PRINT msg$` when Python `msg` is an `int`) raises the dialect's "Type mismatch" error.

Type rules:
- **Numbers convert; other types do not.** A plain BASIC variable reading a Python `int` gets a float. `n%` reading a Python float truncates toward zero, BASIC's own rule for `A%=1.5`.
- A string read as a number, or a number read as a string, is "Type mismatch". So is any other Python type (dict, function, ...), except a list read as an array (`DIM`).
- **Booleans:** Python `True` reads as BASIC's `TRUE`, -1 in both dialects, and `False` as 0, so `IF flag THEN` behaves the same in both languages.
- BASIC system variables (e.g. BBC `@%`, `TIME`) are not exported.

See Open Questions §16 for unresolved collision cases.

---

## 8. Break key (decided)

- **ESC** interrupts a running BASIC program or Python code (equivalent to ESCAPE on the BBC, RUN/STOP on the C64). ESC exists on all keyboards, including Mac keyboards, which lack Pause/Break.
- In BASIC, execution can be resumed with `CONT`.
- In Python, ESC raises `KeyboardInterrupt`.
- How: while the interpreter runs code, it has its console turn ESC into a break for the interpreter (the console's "ESC means break" setting, §11.4), which MicroPython checks as it runs. A program opts in to catching breaks (`libc-plan.md`, step 21). At the prompt the setting is off and ESC is an editor key.
- At an empty prompt, ESC has no effect *(proposed: clears the current input line if not empty)*.
- **Foreground programs** (§5.2) are *not* interrupted by ESC, because full-screen programs (editors, games) need ESC for themselves. They are interrupted with **Ctrl+C**, as on the Amiga, delivered to whichever screen is in the foreground: the console sends a break, which KoraCLI passes on to the program. A program that has not opted in to catching breaks ends; one that has gets the break and can still be ended with a second Ctrl+C. Background jobs are stopped with `kill`.

---

## 9. Extensions for a modern machine (proposed)

### 9.1 Calling Python from BASIC
- **BBC BASIC:** if `PROCname` or `FNname` is not defined in the BASIC program, it is looked up in the Python namespace. `PROCled_on(17)` calls Python `led_on(17)`.
- **Classic BASIC:** `SYS` with a number keeps its original meaning (call machine code at that address, consistent with KoraOS having no memory protection). `SYS "name", args` calls a Python function or OS command.
- **BBC BASIC:** `SYS "name", args` likewise, echoing RISC OS's `SYS "OS_…"`.

### 9.2 Memory access
- `PEEK`/`POKE` (Classic) and the `?`, `!`, `$` indirection operators (BBC) operate on real memory.
- Python equivalents: `peek(addr)` / `poke(addr, value)` (plus word-sized variants).
- Hex literals: `&FF` (BBC), `0xFF` (Python), `&HFF` (Classic, as in MSX/GW-BASIC).

### 9.3 GPIO
- GPIO registers are memory-mapped, so `PEEK`/`POKE` on them works directly. (Base addresses differ between Pi 3 and Pi 4; to be verified against the peripheral documentation.)
- Friendly layer on top, the same implementation for both languages:
  - BASIC: `PIN 17 OUT`, `PIN 17 = 1`, `x = PIN(4)`
  - Python: `pin(17, OUT)`, `pin(17, 1)`, `x = pin(4)`

---

## 10. Screen, graphics and sound (decided in principle)

- One **graphics/sound library**, used by both languages. Its interface is Python; its drawing primitives (plot, line, circle, fill, blit) are a native C module, because pixel loops in Python are too slow.
- BASIC keywords are thin wrappers over it: `CLS`, `MODE`, `COLOUR`/`COLOR`, `PLOT`, `DRAW`, `CIRCLE`, `SOUND`, etc., spelled according to the dialect.
- Python exposes the same functions (`cls()`, `mode()`, `plot()`, …).
- A program asks for a **graphics surface**, not for a screen, so where the surface is shown can change without changing programs.

### 10.1 First: a canvas screen (decided)
The graphics surface is a screen of its own (as `gfxdemo` opens today), in front while the program draws. The user returns to KoraCLI when the program ends, or with Meta+M. This costs no new console protocol.

The known limit: at the prompt, immediate-mode `PLOT` draws on another screen, so the prompt and the drawing cannot be seen together.

### 10.2 Later: a graphics layer over the console (proposed)
The graphics surface becomes a layer of KoraCLI's own screen, composited with the text with transparency, possibly on the GPU (the Pi's display hardware blends planes, or GL later). The screen manager composites, as it already puts client pixels on the display, so the console task needs no graphics code.

- Each clear clears its own layer: `CLS` the text, `CLG` the graphics (BBC BASIC has both already; Classic BASIC's `CLS` clears both *(proposed)*).
- Text scrolls; graphics stay where they are drawn.
- The text layer needs a transparent background, at least for cells with the default background.
- Which layer is on top is to be decided (text over graphics is the BBC feel).

---

## 11. Editing

### 11.1 Two editor styles, user-selectable (decided)
1. **Full-screen (C64-style):** the cursor can move anywhere on screen; pressing Enter on any visible line re-enters it. Editing a listed BASIC line and pressing Enter replaces it. Also used to edit Python blocks in place.
2. **Line-oriented (modern, IPython/xonsh-style):** Up/Down recall history; the screen above the prompt is read-only.

The default is *to be decided* (see Open Questions).

### 11.2 Multi-line Python blocks (decided)
- An indented block continues on the next line with the `...` prompt.
- **One blank line** inside a block is kept as part of the block (so functions can contain blank lines).
- **Two consecutive blank lines** end the block and execute it.
- The whole block remains editable before it is submitted.

### 11.3 History (decided)
- **PgUp / PgDn** move through history **by block** (a whole multi-line Python block is recalled as one unit).

### 11.4 Implementation (decided)
- The editor is **written in Python**, a module next to the dispatcher, so users can read and change it like the rest of the shell. Parts may move to C later if they turn out too slow; typing speed (a key every 100 ms or more) leaves ample margin.
- If the editor fails (an exception, a broken edit to its module), the interpreter falls back to MicroPython's built-in line editor (C), so there is always a prompt to fix it from.
- The editor reads **key events**, not bytes: a key-event mode of the console, next to cooked and raw, delivers each key's code, modifiers and character. ESC, the F-keys and Alt are then unambiguous, with no escape sequences to parse. Programs started from KoraCLI still get cooked or raw bytes.
- While the interpreter runs code, it switches the console's "ESC means break" setting on (§8).

---

## 12. Files (decided)

### 12.1 BASIC programs
- Saved as **plain text** with the `.BAS` extension.
- `SAVE` automatically writes a **dialect header as line 0**:
  ```
  0 REM !KoraBasic:Classic
  ```
  or
  ```
  0 REM !KoraBasic:BBC
  ```
- `LOAD` reads line 0 to determine the dialect. If it differs from the active dialect, the shell applies the dialect-switch rule (§6.3: warn and confirm).
- `RENUM` never moves line 0.
- A `.BAS` file without a header is loaded in the active dialect *(proposed)*.

### 12.2 Python files
- `.py` files are **pure Python**. Shell syntax (`!`, `%`, line numbers) is not interpreted inside them.
- If scripts with shell syntax are wanted later, they get a separate extension.

---

## 13. Help (decided)

| Context | Syntax |
|---|---|
| Python | `help(plot)` or `plot?` |
| BASIC | `HELP PLOT` |
| Later | **F1** on a keyword opens a side panel with a manpage-style entry and examples (in the spirit of Windows 3.x context help) |

Help text for BASIC keywords and the matching Python functions should come from a single source.

---

## 14. Configuration and startup (proposed)

- A startup file (in the spirit of AUTOEXEC.BAT or RISC OS `!Boot`) runs at boot. It is a Python file.
- Configuration covers at least: active dialect, default mode, editor style, F-key assignments, screen mode/font.
- Later, the configuration is reachable from an icon on the right of the top bar.

---

## 15. Error messages (decided)

Each language reports errors in its own style:

| Language | Example |
|---|---|
| BBC BASIC | `Mistake at line 10` |
| Classic BASIC | `?SYNTAX ERROR IN 10` |
| Python | Standard MicroPython traceback |

---

## 16. Open questions

1. **Same name, different types in BASIC.** Classic BASIC allows `A`, `A%`, `A$` and the array `A()` to coexist as separate variables, but they all map to Python `A`. Options:
   - (a) last write wins, with a type check on read;
   - (b) keep them separate inside BASIC and export only by explicit rule (e.g. Python sees `A`, `A_int`, `A_str`);
   - (c) disallow coexistence in KoraOS BASIC (breaks some old listings).
2. **Case in Classic BASIC when reading Python names.** Classic BASIC is case-insensitive, but names are exported with their case. If Python defines `msg` and a Classic program reads `MSG$`, which variable is it? Proposal: exact match first, then a case-insensitive match against Python variables; error if more than one matches.
3. **Default editor style.** Full-screen (more retro, nicer for Python blocks) or line-oriented (more familiar to modern users)?
4. **PgUp/PgDn in full-screen mode.** In the full-screen editor these keys would naturally scroll the screen/scrollback, which conflicts with block history. Separate keys may be needed.
5. **Submitting a block in full-screen mode.** Does the two-blank-lines rule also apply there, or should there be an explicit key (e.g. Ctrl+Enter) to submit immediately in both styles?
6. **Classic BASIC prompt.** `Ok` (MSX/GW-BASIC) or `READY.` (C64)?
7. **Scrollback.** Size and how it interacts with the graphics layer.
8. **`!` command argument syntax.** *Resolved in v0.4:* bare words are strings, `{expr}` interpolates (§5.2, Arguments).
9. **Background job output.** Where does output from a background job go: straight to the screen (interleaved with the prompt, as in Unix shells), or buffered until the user asks for it?

---

## 17. Suggested phasing

| Phase | Scope |
|---|---|
| **1. MVP shell** | Fullscreen layout with top/bottom bars; Python mode on MicroPython; input dispatch (§4); ESC break; line-oriented editor; block history |
| **1b. OS commands** | `!` resolution (wrapper → path → explicit path); live vs captured runs; `kora.run` / `kora.capture`; `str` subclass result; wrapper registration; OS mode (`$`); redirection; plain `&` and `@N`; the old shell's built-ins as wrappers; Ctrl+C for foreground programs; `shell.c` removed |
| **2. Classic BASIC** | Interpreter module; program store; immediate commands; shared variables; SAVE/LOAD with line-0 header; dialect error messages |
| **3. BBC BASIC** | Second dialect; multi-line constructs; `PROC`/`FN` fallback to Python; star commands; dialect switching |
| **4. Machine access** | PEEK/POKE, indirection operators, `SYS`, GPIO layer |
| **5. Graphics & sound** | Shared library (native drawing primitives); BASIC keywords; canvas screen (§10.1) |
| **6. Editing & help** | Full-screen editor; `HELP` / `help()` / `?`; F-key configuration; startup file |
| **7. Jobs** | Job objects (`job = !cmd &`); `jobs` / `fg` (plain `&`, `@N` and `kill` come in 1b) |
| **8. Performance & extras** | BASIC → Python transpile cache; F1 context help panel; paren-less call sugar; rich wrapper objects for built-in commands; pipes in OS mode |
