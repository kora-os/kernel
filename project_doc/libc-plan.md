# libc plan (Milestone 12)

KoraOS grows its own small C library in `user/libc` instead of porting musl,
picolibc or newlib. musl is written against the Linux syscall interface (TLS
through `TPIDR_EL0`, `clone`/`futex`, signals, `brk`/`mmap`, `-errno` returns,
Linux struct layouts), so it would be a permanent fork; the embedded libcs
would fit better, but a native libc keeps the Amiga-style volumes, assigns and
`spawn` model first class. Pure algorithms where hand-written code is usually
subtly wrong (`libm`, float parsing and printing) may be borrowed later under
their own permissive licences.

## End goals

1. **koracli**: the new full-screen shell (menu, status bar). Not part of this
   plan; it gets designed separately once stdio is in place.
2. **MicroPython** as KoraOS's built-in scripting language, in the spirit of
   BBC BASIC and ARexx: a `kora` module over the OS API, a native `os.path`
   for volume/assign paths, and later ARexx-style named message ports.
   MicroPython needs little from libc (string, stdlib, setjmp, file I/O), but
   wants floats in EL0 and a usable stdio.

CPython is out of scope.

## Shape of the library

- Public headers in `user/libc/includes` are strictly ISO C: standard names,
  signatures and semantics, nothing KoraOS specific. A later swap to another
  libc then only means re-implementing `src/sys`.
- The OS API (spawn, threads, jobs, volumes, assigns, `forbid`/`permit`,
  framebuffer, terminal) stays in libk, which links against libc.
- Every libc syscall goes through `__syscall()` (`src/internal/syscall.h`), so
  host unit tests replace the kernel with `tests/fake_syscall.c`.
- `errno` is `(*__errno_location())` from the start, so it can become per
  thread once the kernel saves `TPIDR_EL0`, without an ABI change.
- Floating point is allowed in libc and in user programs (except `fpprobe`,
  which checks the registers by hand): the kernel switches FP/SIMD state
  lazily. `long double` is IEEE binary128 on
  AArch64 and has no hardware support; libc never does arithmetic in it
  (there is no compiler-rt), only bit conversions: `strtold` and the `%L`
  printf conversions work at double precision.

## Steps (one PR each)

Every step adds host unit tests (ASan/UBSan, `tests/run-libc-tests.sh`) and,
from step 4, on-target checks in the QEMU smoke tests. CI runs both.

| # | Theme | Status |
|---|-------|--------|
| 0 | string, ctype, stdlib conversions, malloc and the syscall layer | done (#76, #77, #78) |
| 1 | Cross-build libc in CI with an undefined-symbol link check; FP allowed in libc; `strtold` without binary128 arithmetic | done (#79) |
| 2 | `errno.h`; freestanding headers (`stddef.h` completed, `stdint.h`, `limits.h`, `stdbool.h`, `stdarg.h`, `float.h`, ...); `strto*` overflow with `ERANGE`; `strerror` | done (#80) |
| 3 | Rest of `stdlib.h`: `abs`/`div` families, `qsort`, `bsearch`, `rand`/`srand`, `atexit`/`at_quick_exit`, `getenv` | done (#81) |
| 4 | libc/libk reconciliation: libc owns `crt0`, malloc and `mem*`; libk keeps the OS API and links libc; user programs build against libc and may use FP; on-target `libcprobe` smoke step | done (#82) |
| 5 | `snprintf`/`vsnprintf` family: integers, strings, pointers, all flags and lengths | done (#83) |
| 6 | Float conversions: `%f`/`%e`/`%g`/`%a` and a correctly rounded `strtod` family (exponents, hex, inf/nan) | done (#84) |
| 7 | `FILE` with buffered `stdout`/`stderr`, `printf`/`puts`/`putchar`/`fputs`/`fflush`, flush at exit; the rest of `<string.h>` | done (#85) |
| 8 | File streams: `fopen`/`fclose`/`fread`/`fwrite`/`fseek`/`ftell`/`fgets`/`fgetc`/`ungetc`, `remove`, `rename`; libk's `rename` moves to libc | done (#86) |

## Userland migration (one PR each)

With stdio in place, the existing user programs moved from libk's helpers
(`kputs`, `kput_int`, `kstrlen`, `kstreq`, raw `write(1, ...)`) to the libc,
and the helpers are gone. libk keeps what is KoraOS specific: the syscall stubs, threads, jobs, `spawn`
and `wait`, the mutex and atomics, `uptime_us`, volumes and assigns, the
framebuffer, `kgetline` and `kheap_info`. The POSIX-shaped calls (`open`,
`read`, `write`, `stat`, `readdir`, ...) stay in libk as KoraOS calls; a POSIX
layer (`<unistd.h>`, `<dirent.h>`) is a later decision, with MicroPython.

No step touched the kernel. Program output stayed byte for byte the same,
so the QEMU smoke tests were the regression check, next to the host tests.

| # | Theme | Status |
|---|-------|--------|
| 9 | Harness and flush rules: the host tests that compile `user/cp.c` and `user/shell.c` build against the libc and its fake kernel; `kgetline` and `spawn` flush `stdout` first; a CI check for literal console fds | done (#88) |
| 10 | File tools on stdio: `cat`, `cp`, `ls`, `mkdir`, `mv`, `rm`, `rmdir`, `echo`, `hello`, `init`, `ps`, and `filetools.h`; errors go to `stderr` | done (#89) |
| 11 | Demos: `gfxdemo`, `termdemo`, `jobdemo`, `pindemo` | done (#90) |
| 12 | The shell: a mechanical migration, no redesign (koracli replaces it) | done (#91) |
| 13 | Test probes in `tests/user`, except `fpprobe`; a formatted message to an unbuffered stream is one write | done (#92) |
| 14 | libk cleanup: remove the helpers the libc duplicates, remove the host-test fallback from `koraos.h`, update the userland guide | done (#93) |

Rules for the migration, which come from the console plan
(`console-plan.md`):

- **No literal console fds in programs.** stdin and stdout become handles to
  a console instance, so programs use `stdin`, `stdout` and `stderr`, never
  `write(1, ...)` or `read(0, ...)`. `tests/check-console-fds.py` enforces it
  with a short allow-list: `kgetline` in libk, `fpprobe` (no libc call may
  run between filling and checking its FP registers, so it also starts its
  child with the raw `libk_spawn_flags`) and `threadprobe`, which tests the
  kernel itself: a thread blocked in a console read, and lines written with
  `write()` from every core at once (the stream's lock would serialize
  them). Programs not migrated yet are listed there too, and the list can
  only shrink.
- **Errors go to `stderr`.** Both streams reach the same console today; the
  split is what lets the console plan decide later where `stderr` goes.
  `stderr` is unbuffered, but one `fprintf` to it is one write, so a message
  stays whole while programs on other cores print.
- **No investment in line input.** Line editing, history and the raw/cooked
  switch move to the screen manager, which deletes `kgetline`. Until then it
  only gains a flush of `stdout`, and the libc's `stdin` stays as it is.
- **Programs that draw with escape sequences flush explicitly.** `stdout` is
  line buffered, so a cursor move or a colour change with no newline waits in
  the buffer: finish a frame with `fflush(stdout)`.
- **A program flushes `stdout` before it hands the console over**: before it
  reads the console directly and before it starts a child. libk does this in
  `kgetline` and `spawn`, the libc in `stdin`.

What a program costs on disk after the migration, measured in step 10: the
stdio core adds about 10 KB to a program (`echo`, `hello`), and `printf`
about 16 KB more (`ls`, `ps`, the file tools: 13 KB before, 40 KB after).
The float conversions are 5 KB of that, so linking them only on demand
would save little.

## What the console plan will ask of libc

None of this is part of the migration; each item belongs to the console step
that needs it.

| Console step | libc change |
|---|---|
| 3. Handles | The standard streams take their handles at startup instead of the fixed fds 0, 1 and 2 in `src/stdio/file.c`. With `isatty`, `stdout` is fully buffered when it is not a console. |
| 4. Screen manager | In cooked mode `stdin` may read a whole line per call instead of one byte, and `fgets(stdin)` replaces `kgetline`. |
| 4. Ctrl+C | Nothing while a break is a plain `kill`: the program loses at most its unflushed output. A break a program can catch (MicroPython's `KeyboardInterrupt`) needs `<signal.h>`. |
| Fonts | Nothing for bitmap fonts. FreeType would need `<setjmp.h>` and part of `<math.h>`. |

Unicode needs nothing: `printf` passes UTF-8 bytes through and the console
library decodes them itself. The streams already take a lock, so the threads
of a screen manager can share them.

After the migration: the console and screen work (`console-plan.md`), koracli
on top of it (separate design discussion), then MicroPython.

## Known kernel gaps (not part of these steps)

- Syscalls return `-1` with no error code, so libc picks the most likely
  `errno` for a failed call.
- No per-thread `TPIDR_EL0`: `errno` is shared by a program's threads.
- No wall clock (`time()`), no `fstat`/`isatty` on an fd, no environment
  passed at spawn (`getenv` returns `NULL`).
- The console is raw: `stdin` gets no echo and Enter arrives as `'\r'`, so
  line input needs the program's own editing (libk's `kgetline`). The
  terminal design is in `console-plan.md`.
- `lseek` cannot go past the end of a file, so neither can `fseek`.
- A program whose main thread calls `thread_exit()` ends when its last thread
  returns, without going through `exit()`: no `atexit` handler runs and no
  stream is flushed. Complete lines on `stdout` are already out; anything
  else needs an `fflush` or an `exit()`.
- Not provided yet: the `scanf` family, `tmpfile`/`tmpnam`, `<time.h>`,
  `<math.h>`, `<setjmp.h>`, `<signal.h>`, `<locale.h>`, `<wchar.h>`.
- The main thread has a single 4 KB stack page: libc keeps large buffers off
  the stack, but exactness costs some: printing a float takes about 1.5 KB
  (its exact decimal digits), `strtod` about 750 bytes.
