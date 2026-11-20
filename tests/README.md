# Tests

## Host unit tests (`tests/host/`)

Pure-logic kernel code (code that does not touch hardware registers) is compiled
with the **host** compiler and run directly on your machine, under
AddressSanitizer and UndefinedBehaviorSanitizer. No cross toolchain, QEMU or
Raspberry Pi is needed, and a run takes a few seconds.

```bash
tests/run-host-tests.sh
```

Requires `clang` (the default `CC`; Apple's clang works), Python 3 and mtools
(`mformat`, `mmd`, `mcopy`) and dosfstools (`fsck.fat`) for real FAT32 fixtures. GCC is not supported:
it rejects the kernel's own `strlen` declaration under `-Werror`.

### Writing a test

- Add `tests/host/<name>_test.c` and a `run_test <name> <kernel sources...>` line
  in `tests/run-host-tests.sh`. Set `TEST_FLAGS` around the line for extra
  compiler flags (the spinlock and kmalloc tests use it).
- Include `test.h` and the kernel header under test. Do **not** include system
  headers: the kernel's `common.h` defines its own fixed-width types, which
  clash with `<stdint.h>`. `test.h` declares the few libc functions tests need.
- Provide stubs for kernel services the code under test calls (for example the
  frame allocator), in the test file itself.
- Use `CHECK(cond, "message %d", ...)` and end the file with
  `TEST_MAIN(test_a, test_b, ...)`.

Current suites:

| Test | Covers |
|------|--------|
| `fat_namespace_test.c` | Create/exclusive-create, Greek/astral/maximal LFNs, strict UTF-8 and FAT names, short-alias collisions and lookup, directory growth/slot reuse, unlink/mkdir/rmdir/rename, object/cwd/assign guards, relative and qualified paths, cross-volume rejection, observed-allocation OOM sweeps, transient/persistent I/O recovery, raw VFAT/dot-link validation and fsck/mtools checks |
| `fat_write_test.c` | Existing-file writes on disposable 64 MiB mtools media: sector preservation, append, fragmented chains, first allocation, shared handle metadata, truncate and freed-cluster reuse, disk full, partition-view write/flush capabilities, FAT mirroring/active FAT/high bits, stale FSInfo, dirty eviction, retry after injected read/write/flush errors, cold remount, fsck and host readback |
| `user_mem_test.c` | `src/proc/user_mem.c`: per-task page-run records behind `alloc_pages`/`free_pages`, foreign and double frees, reclaim on teardown, no leaks on failure |
| `filesystem_test.c` | `src/fs/fat32.c` and `src/fs/namespace.c` on mtools-made volumes: independent geometry/caches, root/BPB labels, duplicate labels/device collisions, relative paths, cwd isolation, retained handles, malformed geometry/chains/LFN, allocation and I/O errors, assign snapshots/replacement/removal/capacity, c-only command lookup and shadowed-label cwd, owned cwd/assign ancestor pins, busy reset and flush-failed reset preservation/retry |
| `blkdev_test.c` | `src/fs/blkdev.c`: bare FAT32 and mixed MBR discovery, hidden FAT32 types, invalid/range/overlap tables, partition-relative bounded read/write/flush, read-only and unsupported I/O, registry capacity |
| `placement_test.c` | `src/proc/placement.c`: least-loaded core with ties to the lowest, offline cores skipped, even spreading, pinned requests honoured or refused (offline or nonexistent core) |
| `spinlock_test.c` | `src/arch/spinlock.c`: one thread per simulated core; lock/trylock/irqsave bookkeeping, panics on recursive locking and foreign unlock (in forked children), and a contended counter with no lost updates or overlapping critical sections. Runs twice: on arm64 hosts the real `LDAXR`/`STXR` code, and the compiler-atomic fallback (`SPINLOCK_PORTABLE`) |
| `kmalloc_test.c` | `src/mm/kmalloc.c`, `src/mm/kmalloc_stress.c`: alignment, zeroing, cache-line separation, block reuse, slab release, page runs, over-aligned blocks, exhaustion, invalid and double frees, balanced IRQ masking, seeded stress |
| `input_test.c` | `src/input/input.c`, the input event queue: order, the uptime stamp, a wake-up for each queued event and none with the queue's lock held, a full queue dropping and counting the newest events while it keeps the others, wraparound from several ring positions |
| `input_tty_test.c` | `src/input/input_tty.c`, the adapter from key events to console bytes: characters, Enter/Tab/Backspace/Escape, the xterm sequences of the navigation keys, Ctrl folding of letters and punctuation, Alt and GUI ignored, Shift+PageUp/PageDown as scrollback requests, key up and repeat |
| `hid_keyboard_test.c` | `src/input/hid_keyboard.c`, the USB keyboard's boot reports as key transitions: presses and releases across slots, the eight modifier keys one at a time, the order within a report, rollover reports, error codes and duplicate usages |
| `virtio_keymap_test.c` | `src/drivers/virtio_keymap.c` with `src/input/input_tty.c`: the US layout, modifiers, Caps Lock and repeats must type the bytes the VirtIO keyboard typed before events existed; then the events themselves (HID usage of every evdev key, modifier bits, repeat flag, characters) |
| `virtio_input_test.c` | `src/drivers/virtio_input.c` on a fake VirtIO transport: discovery, queue setup, one event posted for each key transition, receive buffers reposted, malformed completions stopping the device |
| `port_test.c` | `src/ipc/port.c` with a stubbed scheduler: names (unique, at most 15 bytes, exact match) and generation-carrying ids that a deleted port's successor does not reuse, the port limit, FIFO order with sender pids, the in-flight rules (no message 0, a reply port of one's own, one flight at a time, 128 at most), replies and releases, `port_wait` sleeping until a put and giving up when killed or its port is deleted, a deleted port returning its messages, and process exit: ports deleted, queued messages withdrawn, held ones returned, an ended sender handed back to the scheduler only when its last held message is replied; the port lock balanced and never nested |
| `term_render_test.c` | `user/libconsole/term_render.c`: glyphs from the 8x8 font and `term_glyph()`, SGR colours, reverse/underline/strike/bold, the three cursor shapes and the old cursor erased, a hidden cursor and none in the history view, only dirty rows redrawn, clip rectangles cutting cells, terminals at negative coordinates and past the surface's edges, row padding untouched |
| `term_test.c` | `user/libconsole/term.c`, built as the kernel builds it: the host hooks (two allocations, junk memory still giving blank screens, no history, too little memory, no reply hook, clamped size), autowrap, scrollback and its view, scroll regions, insert/delete/erase, alternate screen, status replies, colours (16/256/24-bit, bce), UTF-8 and DEC line drawing, tabs, origin mode, cursor style/visibility, OSC, REP |

The scratch MBR fixture suite is independent of cross compilation and QEMU:

```bash
tests/run-mbr-image-tests.py
```

It checks aligned multi-partition layout, byte-for-byte source preservation,
invalid-input rejection and preservation of an existing output on failure.

`tests/run-filesystem-tests.sh` also runs independently. It creates disposable
2 MiB and 4 MiB mtools images with distinct content and labels, then makes
narrowly corrupted copies for chain and LFN tests. Fixtures live under
`build/host-tests/fs-fixtures`; the shared userfs is never used as writable media.

The write suite also runs independently:

```bash
tests/run-fat-write-tests.sh
```

It creates a pristine 64 MiB image, hashes it, and mutates disposable in-memory
copies. Three normal images cover valid, unknown and stale FSInfo metadata;
24 additional images cover interrupted allocation and shrink transport writes.
After recovery and cold remount, every exported image must pass `fsck.fat -n`
and exact mtools content readback. The source SHA256 must remain unchanged.
Forced disk-full BAD-cluster reservations and corrupt chains are separate
in-memory fault fixtures with no filesystem-integrity claim. All fixtures live
under `build/host-tests/write-fixtures`; the immutable shared userfs image is
never writable media.

## libc unit tests (`user/libc/tests/`)

The libc is its own CMake project. One script cross-compiles it for AArch64
(with `-Werror`), links the whole archive into a user-style PIE so that any
undefined symbol fails (for example a compiler-rt helper such as `__multf3`,
which KoraOS does not have), and then builds and runs the host unit tests
under ASan/UBSan. CI runs it in the `Host unit tests` job:

```bash
tests/run-libc-tests.sh
```

The script also compiles every public header on its own for AArch64 in C99,
C11 and C17 with `-pedantic-errors` (`libc_header_check`).

Inside a configured kernel build directory the same targets exist
(`cmake --build build --target koraos_libc libc_link_check libc_header_check libc_tests`).

Each `<name>_test.c` is built with the libc sources it covers; add one with
`libc_host_test(<name> SOURCES ...)` in `user/libc/CMakeLists.txt`. The libc
headers shadow the host's, and `host_rename.h` renames every libc symbol
(`malloc` becomes `kora_malloc`, and so on) so the code under test never
collides with, or is silently replaced by, the host libc or the sanitizer's
interceptors. Code that makes syscalls goes through the single `__syscall()`
entry; tests link `fake_syscall.c`, a fake kernel serving page runs (with a
page budget to simulate exhaustion), `yield`, `exit`, a console (captured
fd 1 and 2 with optional short or failing writes, scripted fd 0), a small
in-memory filesystem with the kernel's rules (with optional short reads and
writes and writes that start failing, `mkdir`) and a counted `sync`, in
place of `src/sys/syscall.S`. `fake_screen` is fd 1 and 2 together in the
order written, for checks on what comes out first. A test can install a handler for the calls the fake
kernel does not serve (`fake_set_handler`). `test.h` reaches the host's own `printf` and `fflush` by
symbol name (`test_printf`), since the libc's are renamed for the code under
test.

| Test | Covers |
|------|--------|
| `string_test.c`, `ctype_test.c` | `src/string/string.c`, `src/ctype/ctype.c` |
| `string_search_test.c` | `src/string/search.c`: `memchr` at every position and length, `strchr`/`strrchr` (the NUL is searchable, bytes above 127), `strspn`/`strcspn`/`strpbrk`, `strtok` with changing delimiter sets, `strcoll`/`strxfrm` in the C locale |
| `stdlib_convert_test.c` | `ato*`, `strto*`: white space, signs, bases 0/2–36 and prefixes (a bare `0x` is `0`), end pointers (at the start when there are no digits), clamping with `ERANGE` at every limit, `EINVAL` for a bad base, errno untouched on success. The float conversions have their own test |
| `headers_test.c` | The freestanding headers (`stddef.h`, `stdint.h`, `limits.h`, `stdbool.h`, `stdarg.h`, `float.h`, `stdalign.h`, `iso646.h`): static assertions in `headers_static_checks.h` (also compiled for AArch64) plus runtime checks such as `va_arg` past the register arguments and `va_copy` |
| `errno_test.c` | `errno` storage, distinct codes, `strerror` for every code and the unknown fallback |
| `binary128_test.c` | `src/internal/binary128.h`: the integer-only double/binary128 conversions behind `strtold` and `%Lf`. Widening (normals, signed zeros, subnormals, infinities, NaN payloads) and narrowing (ties to even, carries, overflow, subnormal results), as bit patterns; 100 000 random round trips |
| `stdlib_exit_test.c` | `exit`, `_Exit`, `quick_exit` and `abort` reach `SYS_EXIT` with the right status; `atexit`/`at_quick_exit` handlers run last first and once, a handler may register another, the 32-handler limit, `_Exit`/`abort` run none |
| `signal_test.c` | `signal` and `raise`: handlers installed and returned for the six ISO signals, other numbers and `SIG_ERR` rejected with `EINVAL`, `raise` calls the handler in the caller and leaves it installed, `SIG_IGN`, the default action (exit code minus the signal number), a handler raising its own signal or installing another; `abort` raises `SIGABRT` and ends the program even if the handler returns or the signal is ignored, unless the handler jumps out |
| `setjmp_test.c` | `src/setjmp/setjmp.S`, on arm64 hosts only (it is AArch64 assembly; no sanitizers): return values, `longjmp(env, 0)` giving 1, a jump out of 40 frames, independent buffers, and `tests/setjmp_registers.S`, which fills x19 to x28 and d8 to d15, jumps and reports every register, frame pointer or stack pointer that did not come back. libcprobe runs the same checks on the target, which is where CI (x86-64 hosts) covers them |
| `math_test.c` | `<math.h>` (`src/math`, musl's libm). By hand: the special values of C11 Annex F for every function (signed zeros, infinities, NaNs, domain errors, poles), results that must be exact, `lgamma` against correctly rounded values, the classification and comparison macros for all three types, `nexttoward`, and each `long double` function against its `double` one. Differential (`math_test_impl.h`, once for `double` and once for `float`): about two million results against the host's libm over special values, random bit patterns and random arguments in each function's interesting range; bit for bit for the fully specified functions (`sqrt`, `fma`, `fmod`, `remainder`, `remquo`, the roundings, `frexp`, `ldexp`, `modf`, `nextafter`, ...), within 4 to 16 units in the last place for the others (the largest seen is 6). It found a bug in musl 1.2.5 (`acosh` of a negative number), fixed by taking musl's later version. On hosts that are not arm64 the fifteen single-instruction functions come from the host (`math_host_arch.c`), so there they are only covered on the target, by libcprobe |
| `stdlib_sort_test.c` | `qsort` over 15 sizes and 6 input shapes (sorted permutation, at most about 2 n log2 n comparisons), 1-byte, 3-byte, 24-byte and unaligned elements; `bsearch` hits and misses at every position |
| `stdlib_misc_test.c` | `abs`/`div` families at the limits (no signed-overflow UB), `rand`/`srand` sequences, range and rough uniformity, `getenv` |
| `stdio_snprintf_test.c` | The `snprintf` family, mostly differential: about 490 000 combinations of flags, width and precision (literal and `*`), length modifier and edge values for `d i o u x X c s`, and about 250 000 for `f e E g G a A` (halfway cases, carries, extremes, random doubles, `%.40f` expansions), must match the host libc's `snprintf` byte for byte. By hand: inf/nan, `%a` subnormals and ties (the host libcs disagree there), `(null)`, `%p`, unknown conversions, truncation and return values, every `%n` length, UTF-8 for `%lc`/`%ls` (precision never splits a character, `EILSEQ`), `EOVERFLOW`. libcprobe repeats a many-argument `snprintf` on the target, whose AAPCS64 `va_list` the hosts do not use |
| `stdlib_strtod_test.c` | `strtod`/`strtof` against the host's (correctly rounded) ones, bits and end pointer: hand-picked hard cases, 40 000 random decimal strings, round trips of random doubles in six formats, and exact midpoints between neighbouring doubles (up to 767 digits, also just above and below them). By hand: inf/nan forms, hex edge cases, errno, inputs past the 800 kept digits |
| `stdio_write_test.c` | Stream output over the fake console: when line, full and no buffering write (including the caller's `setvbuf` buffer and writes larger than it), a formatted message to an unbuffered stream (`fprintf(stderr, ...)`, `perror`) going out in a single write, short writes retried, failing writes (`EIO`, error indicators, per-call return values), `fflush(NULL)`, `perror`, `exit` flushing after the `atexit` handlers and `_Exit` not flushing, and 4 threads printing 1 200 lines with none torn |
| `stdio_handles_test.c` | The standard streams on the standard handles the fake kernel hands out: the console when it has none (asked once, on the first I/O), a file for stdout (fully buffered, `isatty` 0 with `ENOTTY`, the caller's `setvbuf` winning) and for stdin (read ahead), and a console handler's port for all three (a line per write packet, stderr at once, one reply port per stream, a byte per read packet, a vanished handler or a returned packet as `EIO`) |
| `stdio_file_test.c` | File streams over the fake kernel's in-memory filesystem (the kernel's open-flag rules, no seeking past the end, 16 fds): every `fopen` mode, `fgets`/`fgetc`/`fread` (block reads past the buffer, binary data), update streams switching between reading and writing without `fseek`, `fseek`/`ftell`/`fgetpos`/`fsetpos`, `ungetc`, errno for failed opens (`ENOENT`, `EEXIST`, `EISDIR`, `EACCES`, `EINVAL`), the 16-file limit, `remove`/`rename`, short kernel reads and writes completed, a refused write reported as `EIO`, `exit` flushing and closing open files, `stdin` (prompt flushed first, raw bytes), `freopen` of `stdin` and `stdout` |
| `stdio_scanf_test.c` | The `scanf` family. By hand: every conversion and length modifier (each stores a value of its own size and nothing more), widths, `*`, `%n`, scan sets (ranges, negation, `]` and `-` as members), literals and white space in the format, the count returned or `EOF`, clamping out of range, `%p` reading what `printf` writes, and the items that begin like a number but are not one (`0x`, `1e+`, `infin`, `nan(`), which fail as the standard defines. Floats against the libc's own `strtod`, bit for bit: 40 000 random numbers, and items of thousands of digits, since the engine hands `strtod` a shortened text. Differential: 140 000 well-formed inputs (integers in every notation and size, floats, words, sets, `%c`) against the host's `sscanf`: return value, stored value and characters read. Streams: `fscanf` over a 1500-row file with whole and short reads, what an item leaves for the next `fgetc`, and `scanf` on the fake console one byte per read |
| `time_test.c` | `<time.h>` (`src/time`). By hand: known dates from year 0 to 9999, the first and last second a `struct tm` can hold and the failure one second beyond (`EOVERFLOW`), `mktime` carrying fields out of range in both directions, every `strftime` conversion on the standard's example date, the `E` and `O` modifiers, years of other than four digits, ISO 8601 weeks at twenty turns of the year, short buffers, `asctime` and `ctime`, `difftime` at the ends of the range, `time` and `timespec_get` reporting no wall clock, and `clock` over a counter the test supplies (ten years of uptime without overflow). Differential: 200 000 times over two million years against the host's `gmtime_r`, 100 000 sets of fields in and out of range against `timegm`, 100 000 round trips through `mktime`, and all conversions of `strftime`, with `asctime`, on every day from 1999 to 2030 and on 100 000 random moments of the years 1000 to 9999 |
| `malloc_test.c` | `src/stdlib/malloc.c`: alignment, neighbour separation, coalescing, pool growth and release, direct page-run blocks, bad and double frees, `realloc`/`calloc`, exhaustion and failed `realloc`, a 200 000-round stress with `__malloc_info` consistency checks |
| `malloc_threads_test.c` | The same heap under four concurrent pthreads (malloc/realloc/free, direct blocks), then a pool consistency check |

## User program tests (`user/tests/`)

The user programs themselves, and libk's console code, are tested on the host
on top of the KoraOS libc, not the host's: the libc is compiled for the host
as in its own tests (headers first on the include path, symbols renamed) and
the kernel is the same fake one. libk's syscall stubs get a C twin,
`libk_host.c`, which passes each call to the fake kernel; `libk_host_rename.h`
renames them (`read` becomes `libk_read`) so they never meet a host function
of the same name. A program's `printf`, its file streams and its raw `read`
then all end in one fake console and one in-memory filesystem, which the test
scripts and inspects. `tests/run-host-tests.sh` runs them; on their own:

```bash
tests/run-user-tests.sh
```

A test includes the program's source with `main` renamed (`#define main
cp_main`), calls it, and flushes `stdout` before looking at the console,
since `main` returns to the test instead of going through `exit`. Calls the
fake kernel does not serve (spawn, waitpid, cwd, volumes) come to a handler
in the test. Add `user/tests/<name>_test.c` and a `run_test <name>` line in
the script. `tests/check-syscall-abi.py` keeps `libk_host.c` and the rename
header in step with `user/libk/syscall.S`.

| Test | Covers |
|------|--------|
| `libk_console_test.c` | `kgetline`: backspace, CR as newline, escape sequences and control bytes dropped, echo, typed-ahead input left queued, a full buffer, EOF. The flush rules: a prompt printed without a newline is on the console before `kgetline` reads and echoes, and a program's buffered text comes out before its child's (`spawn`, `spawn_flags`) |
| `libk_threads_test.c` | libk under threads, with pthreads standing in for KoraOS threads behind the fake kernel's handler: every atomic operation hammered by 8 threads with no lost update, the mutex excluding and `mutex_trylock`, 16 jobs across cores with their results, a job on a core that does not exist, and no heap block left behind (`kheap_info`) |
| `tools_test.c` | The small tools, each run with an argv and checked for its exit status, stdout, stderr (messages go there) and effect on the filesystem. `echo` and `hello`; `cat` (several files, a missing one reported in order with the output around it, a directory, binary data through short reads and writes); `ls` (directories with a slash, UTF-8 names, missing directory, failing `readdir`); `mkdir`, `rmdir`, `rm` (never a directory) and `mv` (never over an existing file), each carrying on after a failure, syncing once and reporting a failed sync; `ps` (aligned columns, unknown state); `init` (banner before the shell, the shell's exit code, a shell that cannot start) |
| `demos_test.c` | The demos, with the test standing in for a framebuffer (host memory), the clock (`uptime_us`, which `koraos.h` leaves to the test under `LIBK_HOST_TEST`), threads (run to completion when created) and child programs (recorded). `gfxdemo`: gradient and colour fills inside the picture's width, no framebuffer. `termdemo`: its escape sequence stream is byte for byte the one of the original `write()`-per-piece version, in a few dozen writes. `jobdemo`: the result line (cores, times, two decimals of speedup, "1 core"), one job then one per core, bands adding up, the picture and the wait for Enter, a job that cannot start, usage. `pindemo`: rounds and flags of each worker, one line per worker in one write, a worker on the wrong core or killed, a worker that cannot start, the defaults on four and two cores, bad specs, worker mode |
| `condemo_test.c` | `condemo` with the console library built as user programs build it (the libc's headers). As a console handler: the program spawned in the background with condemo's stdin and its port for stdout and stderr, write packets answered with their length and drawn (a newline starts a line), reads refused, every packet replied and the port deleted, a program that cannot start. The two-window demo: its report line, the left console's blue screen, DEC box, 16 colours, attributes and cursor where it reported it, the right console filling its window and clipped at it, frames, and no pixel drawn anywhere else; no framebuffer, a 16-bit one, a screen too small |
| `cp_test.c` | The actual `cp`, on file streams: short reads and writes, an existing destination refused and left untouched, a refused write leaving a partial destination, a failing sync, missing and directory sources, usage; both files closed and one sync on every path |
| `shell_input_test.c` | The actual shell, parser and main loop: redirection (`<`, `>`, `>>`, glued or not, with `&` and `@N`, quoted operators as arguments, the files closed after the spawn, each mistake reported and nothing run); fragmented reads, maximal complete lines, oversized-line draining, next-command preservation, 16/17-token boundary, terminated arguments, maximal assign target, quoted/escaped/empty tokens, literal operator provenance, malformed quoting, background jobs and their reaping, `@N` core pinning; the exact screen of a short session (prompt, typed line, child output and job lines in order); the built-ins (`pwd`, `cd`, `name:`, `volumes`, `assign`, `sync`, `kill`, `help`) and their usage and failure messages, which go to stderr |

## Console fd check

```bash
tests/check-console-fds.py
```

User programs reach the console through `stdin`, `stdout` and `stderr`, never
through `read(0, ...)` or `write(1, ...)`: the console plan
(`project_doc/console-plan.md`) turns a program's stdin and stdout into
handles to a console, and only the streams will follow. The check scans
`user/*.c`, `user/libk` and `tests/user` for a literal 0, 1 or 2 as the fd.
It has a short list of lasting exceptions with their reasons, and a list of
programs the migration to the libc has not reached yet; an entry that is no
longer needed fails the check until it is removed, so that list only
shrinks. `tests/run-host-tests.sh` runs it.

## Integer-only kernel check

```bash
tests/check-kernel-fp.py build/debug/*/kernel.elf
```

Disassembles every object built for each kernel (Circle included) and fails on
any instruction naming an FP/SIMD register, or FPCR/FPSR, outside the
save/restore routines in `src/arch/fpsimd.S`. FP/SIMD registers belong to EL0
tasks and are switched lazily, so kernel code (above all interrupt handlers)
must not touch them; such a bug would corrupt user registers, typically only on
real hardware. Needs `llvm-objdump` (or `OBJDUMP=...`). CI runs it on the debug
and release builds.

The namespace suite also runs independently:

```bash
tests/run-fat-namespace-tests.sh
```

It creates a separate valid 64 MiB mtools source and checks its immutable SHA256.
Normal operations and 72 write/flush interruption cases are exported, plus one
consolidated allocation-recovery image per operation and four persistent-error
images. Every result must pass `fsck.fat -n`, exact mtools readback through short
aliases, FAT mirror equality and an independent raw directory parser checking
UTF-16, LFN ordinals/checksums/padding and dot links. An observed-allocation
sweep fails every heap allocation in each operation and checks previous names,
free-cluster counts and object ownership before retry. Directory extension,
read failures and failed rollback retention are covered too. A valid external
LFN resembling a generated short alias verifies that alias creation reserves
both naming forms. Full-disk BAD reservations remain isolated malformed fault
fixtures without an integrity claim. Images live under
`build/host-tests/namespace-fixtures`.

## Feature status file

```bash
tests/check-features.py
```

Validates `project_doc/features.yaml` (field names, statuses, dates, platforms,
PR numbers, and milestone numbers against `project_doc/roadmap.md`). Needs
PyYAML: `apt install python3-yaml`, or run it with
`uv run --no-project --with pyyaml python tests/check-features.py`.

## Build and shared-image regression tests

```bash
tests/run-build-tests.py
tests/run-userfs-tests.py
```

The CLI suite uses fake tool binaries to check repeatable/all target selection,
shared generation, Debug/Release isolation, first-target compilation database,
paths with spaces, deprecated aliases, validation before cleanup, prepared-image
consumption, failure-before-install gates and launcher argument forwarding. It
also exercises lock lifetime when the wrapper receives SIGTERM or SIGKILL.
It requires Python 3 and runs without cross-compiling or touching an SD volume.

The producer suite uses real Clang/CMake/mtools in an isolated temporary source
copy. It verifies no-op reuse, removed program registrations, header/fsroot
additions/removals, compiler-flag invalidation, Debug/Release userfs reuse and
atomic image failure that preserves the previous valid filesystem. CI runs it
in the userfs-producing job.

## QEMU smoke test

```bash
./build.sh --target qemu_raspi3b
tests/run-qemu-smoke.py --target qemu_raspi3b

./build.sh --target qemu_raspi4b      # QEMU 9.0 or newer
tests/run-qemu-smoke.py --target qemu_raspi4b

./build.sh --target qemu_virt
tests/run-qemu-smoke.py --target qemu_virt --keyboard --disk build/userfs/aarch64/koraos.img --repeat 256
```

The test checks the boot line's build identifier (the git commit), the
boot-time kernel heap self-test, and that every core came online (`--smp N`
on virt, 1 to 4; raspi3b always has four; `--gic 3` for a GICv3 and, on an
Apple Silicon Mac, `--accel hvf` to run on the host's cores), boots to the ELF shell,
runs filesystem/argv/exit-code checks plus an EL0 namespace probe (child cwd
inheritance and isolation, failed path handling, descriptors retained across
chdir, volume/assign enumeration and assign syscall updates), checks shell
`cd`/`pwd`/`volumes`/`assign`, current-directory `ls`, and excess-argument
rejection followed by a valid command, switches to the debug console,
verifies timer/UART IRQ progress, checks with `cpus` that every core is online
running its idle task (cores 1 and up) and that each core's own timer ticks,
runs `heaptest` (seeded kernel heap stress)
and checks that `heap` reports no leaks or bad frees, runs `fpprobe` (a parent
and child each fill all 32 vector registers and FPCR; the child must start from
zeroed registers and the parent's must survive), runs `libcprobe` (the libc
on the target: the binary128 `strtold` path, FP through libc code, `errno`,
`qsort`/`bsearch`, the `rand` sequence, the heap, float formatting and
`strtod`, a many-argument `snprintf`, `setjmp`/`longjmp` with a check of
every callee-saved register, a signal handler that jumps out of `abort`,
`sscanf` (ten conversions through the `va_list`, a float item of a thousand
digits on the 4 KB stack; `libcprobe files` reads its file back with
`fscanf`), `<time.h>` (`clock` on the real generic timer across a sleep, a
date formatted and normalized), `<math.h>` (the single-instruction functions, some forty results that must
have the same bits as on the hosts, and the `long double` functions and
`nexttoward` on real binary128 values), and a fully buffered `stdout` that only
`exit` flushes, after the `atexit` handler; on writable roots also
`libcprobe files`: file streams through the real FAT32 layer, then `sync`),
checks `tasks` (init and the shell
asleep, kernel stack high-water mark under three quarters), runs `schedprobe`
(`msleep(300)` takes at least 300 ms; a background job busy-looping without
syscalls must not stop the shell from running `echo`, so the tick preempts it;
the job is reported as done at a later prompt), runs `forbidprobe` (`forbid()`
nests and survives `msleep`; with a spinner competing, the task loses the CPU
for whole time slices normally but not while forbidden; a held `forbid()`
shows in `tasks` as the BKL holder, which is free again afterwards), checks `ps`
and placement (with more than one core: `@N` pins a job to core N, two
unpinned jobs land on different cores, and two spinners lose whole time slices
when they share a core but not on separate cores; with one core, `@1` fails),
and checks terminal
colors and graphics pixels using QEMU screenshots (`condemo`: two consoles
of the console library in windows, with the right one clipped). `--keyboard` types a
command through a keyboard, a VirtIO one on virt and QEMU's USB keyboard
(the Circle path of the Pi) on raspi3b: Shift/release, an arrow, Backspace,
Enter and scrollback, all of it travelling through the kernel's input event
queue. UART observes output. With a writable root, the shell's redirections
write and append to a file and read one back (`libcprobe handles` reports
`isatty` and its first stdin line from a file); `condemo echo ...` shows
echo's output in its window through message ports and not on the kernel
console. `portprobe` passes messages between processes:
100 round trips and a pipeline, a sender that ends while its message is held
(it stays in `sysinfo().procs` until the reply, and every page comes back),
a receiver that ends holding a message (it comes back `PORT_RETURNED`), and a
program killed while it sleeps in `port_wait`. `sigprobe` checks signals:
allocation, set, clear and wait, a thread waking another (on another core
when there is one), 50 messages and a child's end served from one
`sig_wait`, as a console handler does, and a program killed in `sig_wait`. `--repeat N` runs `allocprobe` N times: page runs, a `malloc`
heap grown to about 1.5 MiB with churn and `realloc`, a nested child while it is
live, and deliberate leaks. The kernel heap's live count and the free page count
must be the same after the last run as after the first, so everything a reaped
task held, leaked or not, came back.

Use `--release` and `--build-dir` to match the build configuration. `--kernel`
and `--out` override the image and artifact locations. Defaults are the selected
`build/debug/<target>/kernel.img` and its `qemu-smoke/` directory. `--machine`
remains a deprecated alias for target selection. `--no-graphics` omits ramfb on
virt and tests serial fallback. `--disk-writable` negotiates writable test media;
file writes require a writable file descriptor. Use writable media only on
disposable scratch images.

On raspi3b and raspi4b, `--sd IMAGE` puts a scratch card from
`tests/create-sd-card-image.py` in the SD slot, and on raspi3b `--usb-stick
IMAGE` (made with `--label USBSTICK`) plugs it into QEMU's USB next to a QEMU
USB keyboard. Each drive's steps find its volume, checksum an odd-sized file
with `sumprobe` from core 0 (where USB interrupts arrive) and from the last
core, run a program from it, and check that `mkdir` is refused. With
`--sd-writable` / `--usb-stick-writable` and a `--storage-writable` kernel they
copy files, make a directory and sync instead; `tests/run-qemu-storage-write.py
--media sd|usb|both` wraps that and checks each drive on the host afterwards
with `fsck.fat`, mtools readback and identical FAT copies.

An external disk may contain a bare FAT32 volume or primary MBR FAT32
partitions. The first supported partition supplies the boot root. To exercise
partition offsets using a separate scratch image:

```bash
tests/create-mbr-image.py --output build/mbr-root.img build/userfs/aarch64/koraos.img
tests/run-qemu-smoke.py --target qemu_virt --disk build/mbr-root.img --repeat 64
```

The `--multi-volume` profile additionally requires a second labeled partition:

```bash
tests/create-multivolume-image.py --output build/multi-root.img build/userfs/aarch64/koraos.img
tests/run-qemu-smoke.py --target qemu_virt --disk build/multi-root.img --multi-volume
```

The fixture copies the shared image, relabels its copies as BOOT and EXTRAS,
forces stale BPB labels, gives EXTRAS distinct README content and a private
`extrahello` command, and verifies the source SHA256 remains unchanged. The
profile assigns `c:` to EXTRAS while cwd remains on BOOT, executes `extrahello`
and `c:/hello`, then verifies missing or removed `c:` does not fall back to
BOOT `/bin`. It restores `c:` before subsequent checks.

The writable profile runs the complete VM-to-host integrity check:

```bash
./build.sh --target qemu_virt
tests/run-qemu-write.py
```

It creates a fresh 64 MiB FAT32 root from `fsroot` and registered prepared ELF
names, excluding stale ELF leftovers. The shared 2 MiB image is hashed and
never used as writable media. `writeprobe` covers creation, exclusive create,
read-only create, zero-byte permissions, large-length rejection, full fd-table
failure before mutation, data growth, partial-sector overwrite, shared append,
truncate, UTF-8 names, namespace and busy guards. Quoted CLI file utilities
exercise durable sync. After QEMU closes, `fsck.fat -n` and mtools verify exact
contents and removed paths; raw checks compare FAT mirrors and clean/error bits.
Logs and `verification.json` live in `build/debug/qemu_virt/qemu-write` by default.
`--out`, `--build-dir`, `--userfs-dir` and `--release` select other artifacts.

`--expect-root-failure --disk <zeroed-image>` requires a selected VirtIO disk,
failed mount and failed init load, so silent ramdisk fallback cannot pass.
Without `--disk` it requires a kernel built without `--ramdisk` to report that
no volume has `/bin/init`.

`--ramdisk MB` tells the smoke test the kernel was built `--ramdisk` with that
size: it checks the boot line, and when the ramdisk is the root (no `--disk`)
runs the EL0 write steps (`writeprobe` and the quoted file tools) on it.
Kernels built without a ramdisk boot from a `--sd` card or `--usb-stick` drive
(their FAT32 copy of the userfs has `/bin/init`). `writeprobe df0:` adds checks
that a read-only volume refuses writes, creates and renames.

`tests/run-ramdisk-image-tests.py` (run by `tests/run-host-tests.sh`) checks
`create-ramdisk-image.py`: size parsing, the blob format, and that the volume
keeps the userfs contents, names and directory order.

## CI coverage

One producer job publishes `koraos.img` and user ELF symbols under a shared
AArch64 artifact. Consumers download it and use `--userfs-dir` without invoking
userland compilation or filesystem generation. The all-target job builds four
Debug kernels, stages both hardware Release payloads into a temporary directory,
creates a FAT32 SD image, and runs raspi3b smoke on a 16M ramdisk. A raspi3b storage job reads
and writes scratch SD cards and USB drives, and a raspi4b job covers the Pi 4
kernel with its EMMC2 card. Separate virt profiles cover
embedded graphics, 128 MiB external disk/keyboard, primary MBR boot, two labeled
partitions with namespace navigation and assign command lookup, 64 MiB
serial-only, EL2 entry and writable scratch-root integrity. UART/screenshots are uploaded on failure. Pure host
sanitizer suites stay
independent of the producer and kernel jobs.

QEMU cannot validate real Pi USB, HDMI, caches or boot firmware. Hardware testing
remains separate. Pi 3 USB IRQ counters include idle frame processing; compare
controller-specific behavior rather than interpreting them as keystroke counts.
