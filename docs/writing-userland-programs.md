# Writing a Userland Program

This describes how to write, build, and run a userland program for KoraOS **as
it is today**: there is no on-device compiler and no libc. Programs are
cross-compiled on the host into position-independent ELF executables, installed
onto the FAT32 image under `/bin`, and loaded from there at runtime.

## The environment you get

A program runs in EL0 with a deliberately small runtime:

- **No libc.** No `malloc`, no `printf`, no `<string.h>`. You have the syscalls
  (see [syscalls.md](syscalls.md)) and a handful of inline helpers in
  [`user/libk/koraos.h`](../user/libk/koraos.h): `kputs`, `kput_int`,
  `kstrlen`, `kstreq`. Anything else, you write yourself.
- **No floating point.** Programs are built with `-mgeneral-regs-only`.
- **A ~4 KB stack.** Each task gets a single-page user stack, so keep large
  buffers off the stack (use small chunks, or `sbrk`).
- **A lazy 64 KB heap** via `sbrk`, if you need dynamic memory.
- **A flat identity map.** Pointers are physical addresses; e.g. `fb_info()`
  hands you the framebuffer address and you write pixels to it directly.
- **Cooperative, nesting processes.** `spawn` runs a child to completion before
  returning (then `wait` reaps it). There is no preemption and no threads.
- **`main` signature.** Either `int main(void)` or `int main(int argc, char
  **argv)`; the entry stub [`user/crt0.S`](../user/crt0.S) calls `main` and
  passes its return value to `exit`.

## The smallest program

```c
/* user/greet.c */
#include "libk/koraos.h"

int main(int argc, char **argv) {
    kputs("hello");
    for (int i = 1; i < argc; i++) {
        kputs(" ");
        kputs(argv[i]);
    }
    kputs("\n");
    return 0;
}
```

`kputs` is just `write(1, s, kstrlen(s))`. Look at the existing programs for
patterns: [`user/hello.c`](../user/hello.c) (minimal),
[`user/echo.c`](../user/echo.c) (argv), [`user/cat.c`](../user/cat.c) and
[`user/ls.c`](../user/ls.c) (the file syscalls),
[`user/init.c`](../user/init.c) (spawning), and
[`user/gfxdemo.c`](../user/gfxdemo.c) (framebuffer).

## Building it

Register the program in [`CMakeLists.txt`](../CMakeLists.txt), in the
"User programs" section, next to the others:

```cmake
add_user_program(greet "${USER_DIR}/greet.c")
```

That is all. `add_user_program` compiles the source together with the userland
runtime (`crt0.S` + `syscall.S`) into a standalone **position-independent** ELF
(`-fPIE -Wl,-pie`), using the same cross toolchain as the kernel:

```
--target=aarch64-none-elf -mcpu=cortex-a72 -ffreestanding -nostdlib
-fno-builtin -fno-stack-protector -mgeneral-regs-only -fPIE -O2
```

The linker is told to emit classic `DT_RELA` relocations
(`--pack-dyn-relocs=none`), because the kernel's ELF loader
([`src/user/elf.c`](../src/user/elf.c)) only handles `ET_DYN` images with
`R_AARCH64_RELATIVE` relocations — no dynamic linking, no other reloc types.

Build as usual (`mtools` is required so the FS image can be assembled):

```bash
./build.sh --qemu
```

CMake installs `build/user/greet.elf` onto the FAT32 image as `/bin/greet` (the
`.elf` suffix is stripped) and rebuilds the embedded image automatically. See
[filesystem.md](filesystem.md) for how the image is put together.

## Running it

Boot and use the shell:

```bash
./run-qemu.sh
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
- No preemption: a long-running program blocks its parent (the shell) until it
  exits. Return from `main` (or call `exit`) to give control back.
- The filesystem is **read-only** — a program cannot create or modify files yet.
