# Writing a Userland Program

This describes how to write, build, and run a userland program for KoraOS **as
it is today**: there is no on-device compiler and the C library is still
small. Programs are
cross-compiled on the host into position-independent ELF executables, installed
onto the FAT32 image under `/bin`, and loaded from there at runtime.

## The environment you get

A program runs in EL0 with a deliberately small runtime:

- **A small C library.** [`user/libc`](../user/libc) provides
  the freestanding headers (`<stddef.h>`, `<stdint.h>`, `<limits.h>`,
  `<stdbool.h>`, `<stdarg.h>`, `<float.h>`, ...), `<string.h>`, `<ctype.h>`,
  `<errno.h>`, `<stdlib.h>` (conversions, `qsort`/`bsearch`, `rand`,
  `atexit`, the heap) and `<stdio.h>` (`printf` and friends, `fopen`,
  `fgets`, `fread`, `fwrite`, `fseek`, `remove`, `rename`; `stdout` is line
  buffered, `stderr` is unbuffered but sends each `fprintf` in one write,
  `exit` flushes and closes everything; `sscanf` and `fscanf` read numbers
  and fields back, `scanf` reads the raw console described below),
  `<setjmp.h>`,
  `<signal.h>` (`signal` and `raise` only: a handler runs when the program
  itself raises the signal, nothing arrives from outside) and `<math.h>`
  (the C99 functions for `double` and `float`, from musl; the `long double`
  ones compute at double precision; no `M_PI`, which is not ISO C) and
  `<time.h>` (`clock` counts microseconds since boot; `mktime`, `gmtime`,
  `strftime` and the rest work in UTC; `time` returns -1, since there is no
  wall clock yet), `<assert.h>` (a failed assertion reports on `stderr` and
  aborts) and `<inttypes.h>`. `stdin`
  is the shell's console, which hands over whole lines (below). See
  [libc-plan.md](../project_doc/libc-plan.md) for what is still missing.
  Programs print through `stdout` and report errors on `stderr`; they never
  name the console by file descriptor (`write(1, ...)`), which
  `tests/check-console-fds.py` enforces. libk
  ([`user/libk/koraos.h`](../user/libk/koraos.h)) flushes `stdout` where the
  console changes hands: before `spawn` starts a child (which writes to the
  console directly). A program
  that draws with escape sequences and no newline ends each frame with
  `fflush(stdout)`. `koraos.h` includes
  `<stddef.h>`, `<stdio.h>`, `<stdlib.h>` and `<string.h>`. The KoraOS calls
  themselves are in [syscalls.md](syscalls.md).
- **Floating point.** C code may use `float` and `double`: the kernel keeps
  each task's FP/SIMD registers and switches them lazily. `long double` is
  IEEE binary128, which the hardware cannot compute with, and there is no
  compiler-rt: avoid arithmetic in it (the link fails on `__addtf3` and
  friends). `strtold` and `%Lf` work at double precision.
- **A ~4 KB stack, unless you ask for more.** A program's main thread gets a
  single-page user stack (other threads get 16 KB), so keep large buffers off
  the stack (use `malloc`, or static arrays). Nothing protects what lies below
  it: a larger frame writes over kernel memory and the kernel faults later,
  somewhere else. The build refuses a function whose frame passes 2 KB
  (`-Wframe-larger-than=2048`); a path (`KORA_PATH_MAX`, 4128 bytes) alone
  passes it. A program that needs more (an interpreter, say) declares its
  main thread's stack when it is linked, `-Wl,-z,stack-size=N` (in
  `cmake/userfs.cmake`, as the program's `FLAGS`), and gets `N` bytes rounded
  up to whole pages, up to 1 MB; a larger request makes the spawn fail. A
  thread asks for its own with `thread_create_stack(fn, arg, core, size)`.
  `task_info` reports each thread's stack (`stack_base`, `stack_size`).
- **A heap that grows with the system's free memory.** `malloc`, `free`,
  `calloc` and `realloc` (16-byte aligned) come from the libc
  ([`user/libc/src/stdlib/malloc.c`](../user/libc/src/stdlib/malloc.c)), a
  TLSF allocator over 64 KB
  pools of pages; blocks over 256 KB get page runs of their own. For whole
  pages, call `alloc_pages`/`free_pages` directly. Whatever you do not free is
  reclaimed when your program is reaped. `kheap_info()` reports the heap's
  pools and usage. The heap is shared by all your threads, behind one lock.
- **A flat identity map.** Pointers are physical addresses; e.g. `fb_info()`
  hands you the framebuffer address and you write pixels to it directly.
- **Preemptive processes on every core.** Each program runs on one core for
  life, the least-loaded one unless you choose (`SPAWN_CORE(n)`, or `cmd @n`
  in the shell); programs on different cores run in parallel, and those on one
  core get 10 ms time slices in turn. `ps` lists them. `spawn` runs a child to completion before returning (then `wait` reaps
  it); `spawn_flags(..., SPAWN_NOWAIT)` starts it in the background, and
  `waitpid(-1, &code, WNOHANG)` collects finished children without blocking.
  `spawn_std` also sets the child's stdin, stdout and stderr (a file of
  yours, or a console handler's port: see `condemo`); `isatty(1)` tells
  whether your stdout is a console. To wait for several things at once (your
  ports, a child's end, another thread), give each a signal bit and sleep in
  `sig_wait` (`port_signal`, `SPAWN_SIGNAL`, `sig_on_end`, `sig_send`);
  `condemo` serves its program's packets and notices its end that way.
  `msleep(ms)` sleeps; `uptime_us()` reads a microsecond clock without a
  syscall. `forbid()`/`permit()` keep the CPU and the kernel to yourself while
  you poke at kernel structures (see [locking.md](locking.md)). `kill(pid)`
  (the shell's `kill <pid>`) ends another program.
- **Threads and jobs.** `thread_create(fn, arg, core)` runs `long fn(void
  *arg)` in a new thread of your program, on `core` for life (`-1`: the
  least-loaded core); everything but the stack and registers is shared.
  `thread_join(tid, &result)` waits for it and collects `fn`'s return value
  (or `thread_exit(value)`'s). Guard shared data with a `mutex_t`
  (`mutex_lock`/`mutex_unlock`). On top, the TempleOS-style job API queues a
  function to a core and collects the result later: `job_t *job =
  job_queue(fn, arg, core)`, then `long r = job_result(job)`. `exit()` from any
  thread (or returning from `main`) ends the whole program, all threads
  included; if the main thread calls `thread_exit` instead, the program runs
  on until its last thread ends. About 32 tasks exist system-wide, so join
  threads before starting many more. `sysinfo()` tells how many cores are
  online. For lock-free counters and flags, libk has atomics on any `int`,
  `long` or pointer: `atomic_get`/`atomic_set` (acquire/release),
  `atomic_add` (returns the new value), `atomic_inc`/`atomic_dec`,
  `atomic_swap` and `atomic_cas`, plus `cpu_relax()` for spin loops.
  [`user/jobdemo.c`](../user/jobdemo.c) and [`user/pindemo.c`](../user/pindemo.c)
  are worked examples of jobs and of pinned sub-processes.
- **Screens.** Under the screen manager (`screens prog [args] + prog
  [args]`, [`user/screens.c`](../user/screens.c)) a program opens a screen
  of its own with libscreen ([`user/libscreen/screen.h`](../user/libscreen/screen.h)):
  `screen_t *s = screen_open("title")` gives a pixel buffer as large as the
  display (`s->pixels`, `s->width`, `s->height`, `s->pitch`), your program's
  own memory, which the manager only reads; NULL when no manager runs. Draw into it, then `screen_damage(s, x, y, w, h)`: the
  manager copies that rectangle to the display while the screen is in front.
  Meta+M brings the next screen to front; the keys go to the front screen's
  program as events, read with `screen_event` after
  `sig_wait(screen_signal(s))`. So do the mouse's (`INPUT_EVENT_MOUSE`): the
  manager draws the pointer, and `x` and `y` are its position on your
  screen, in pixels, with the buttons held and the wheel's turn. If the manager ends, `screen_gone(s)` says so
  (the same signal wakes you): close the screen and stop. `screen_close`, or
  the program's end, closes it. [`user/gfxdemo.c`](../user/gfxdemo.c) is the smallest example. A
  program that drives the display and the keyboard itself (`fb_claim`,
  `input_claim`) first borrows them from the manager with `screens_borrow()`,
  and gives them back with `screens_return()` or by ending.
- **Consoles.** A console on a screen (`user/libscreen/console.h`) is a
  terminal its programs use as stdin, stdout and stderr: the shell runs on
  one, so your program does too, and `newcon program` runs one in a console
  of its own. Reads are cooked by default: the console edits the line
  (arrows, Home/End, history with Up/Down) and a read gets it once Enter is
  pressed, with Ctrl+D as the end of input. `console_mode(CON_RAW)` hands
  your program each key as typed instead, unechoed (arrows as xterm escape
  sequences), and `console_mode(CON_COOKED)` goes back.
  `console_font(psf, size)` draws the console your stdout is in a PSF2
  font (NULL: the built-in 8x8 one), as the `font` command does; the
  console resizes, so ask for its size again (`ESC [ 18 t`) if you lay
  text out by it. Ctrl+C ends the
  program: the shell kills the one it runs in front, newcon its own. On the
  kernel console these do nothing: it is always raw, with no
  editing at all.
- **`main` signature.** Either `int main(void)` or `int main(int argc, char
  **argv)`; the libc's entry stub
  [`user/libc/src/crt/crt0.S`](../user/libc/src/crt/crt0.S) calls `main` and
  passes its return value to `exit`, which runs the `atexit` handlers.

## The smallest program

```c
/* user/greet.c */
#include "libk/koraos.h"

int main(int argc, char **argv) {
    printf("hello");
    for (int i = 1; i < argc; i++) {
        printf(" %s", argv[i]);
    }
    printf("\n");
    return 0;
}
```

`koraos.h` brings in `<stdio.h>`, and the line reaches the console at its
newline, in one write. Look at the existing programs for patterns: [`user/hello.c`](../user/hello.c) (minimal),
[`user/echo.c`](../user/echo.c) (argv), [`user/cat.c`](../user/cat.c) and
[`user/ls.c`](../user/ls.c) (the file syscalls),
[`user/init.c`](../user/init.c) (spawning),
[`user/gfxdemo.c`](../user/gfxdemo.c) (framebuffer), and
[`user/condemo.c`](../user/condemo.c) (consoles of the console library,
`user/libconsole`, drawn in windows on the framebuffer or on a screen; its
registration in `cmake/userfs.cmake` shows how a program links the
libraries' sources, `LIBCONSOLE_SOURCES` and `LIBSCREEN_SOURCES`).

## Building it

Register the program in [`cmake/userfs.cmake`](../cmake/userfs.cmake), inside the
`koraos_add_userfs` function, next to the existing registrations:

```cmake
koraos_add_user_program(greet "${USER_DIR}/greet.c")
```

That is all. `koraos_add_user_program` compiles the source together with libk
(`syscall.S`, the mutex, jobs and heap diagnostics) and links the libc archive
(entry point, C library, malloc) into a standalone **position-independent** ELF
(`-fPIE -Wl,-pie`), using the same cross toolchain as the kernel:

```
--target=aarch64-none-elf -mcpu=cortex-a72 -ffreestanding -nostdlib
-fno-builtin -fno-stack-protector -fPIE -O2 -Wall
-Wframe-larger-than=2048 -Werror=frame-larger-than -isystem user/libc/includes
```

Extra flags for one program go after `FLAGS`, for example
`koraos_add_user_program(fpprobe ... FLAGS -mgeneral-regs-only)`.

The linker is told to emit classic `DT_RELA` relocations
(`--pack-dyn-relocs=none`), because the kernel's ELF loader
([`src/user/elf.c`](../src/user/elf.c)) only handles `ET_DYN` images with
`R_AARCH64_RELATIVE` relocations, no dynamic linking, no other reloc types.

Build as usual (`mtools` is required so the FS image can be assembled):

```bash
./build.sh --target qemu_virt
```

CMake installs `build/userfs/aarch64/user/greet.elf` onto the FAT32 image as `/bin/greet` (the
`.elf` suffix is stripped) and rebuilds the embedded image automatically. See
[filesystem.md](filesystem.md) for how the image is put together.

## Running it

Boot and use the shell:

```bash
./run-qemu.sh --target qemu_virt
```

```
$ greet world
hello world
$ ls /bin
```

The shell resolves a bare command name to `/bin/<name>` and `spawn`s it, so
`greet world` runs `/bin/greet` with `argv = {"greet", "world"}`. You can also
spawn by absolute path.

## Shipping non-program files

To put data files on the image (not programs), drop them under
[`fsroot/`](../fsroot); the tree is mirrored into the image root. Read them from
a program with `open` / `read` / `close`.

## Constraints to keep in mind

- Names/paths are **UTF-8**; a `struct dirent` name can be up to 765 bytes.
- `argv` is capped at 16 entries.
- A program the shell runs in the foreground keeps the shell waiting until it
  exits; run long jobs with `&`. Only one program should read the console at
  a time: concurrent readers would take turns getting characters.
- The filesystem is **read-only**, a program cannot create or modify files yet.
