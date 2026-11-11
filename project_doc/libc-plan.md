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

After step 8: koracli on libc (separate design discussion), then MicroPython.

## Known kernel gaps (not part of these steps)

- Syscalls return `-1` with no error code, so libc picks the most likely
  `errno` for a failed call.
- No per-thread `TPIDR_EL0`: `errno` is shared by a program's threads.
- No wall clock (`time()`), no `fstat`/`isatty` on an fd, no environment
  passed at spawn (`getenv` returns `NULL`).
- The console is raw: `stdin` gets no echo and Enter arrives as `'\r'`, so
  line input needs the program's own editing (libk's `kgetline`); the
  terminal design belongs to koracli.
- `lseek` cannot go past the end of a file, so neither can `fseek`.
- Not provided yet: the `scanf` family, `tmpfile`/`tmpnam`, `<time.h>`,
  `<math.h>`, `<setjmp.h>`, `<signal.h>`, `<locale.h>`, `<wchar.h>`.
- The main thread has a single 4 KB stack page: libc keeps large buffers off
  the stack, but exactness costs some: printing a float takes about 1.5 KB
  (its exact decimal digits), `strtod` about 750 bytes.
