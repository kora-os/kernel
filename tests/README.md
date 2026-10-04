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
(`mformat`, `mmd`, `mcopy`) for real FAT32 fixtures. GCC is not supported:
it rejects the kernel's own `strlen` declaration under `-Werror`.

### Writing a test

- Add `tests/host/<name>_test.c` and a `run_test <name> <kernel sources...>` line
  in `tests/run-host-tests.sh`. Set `TEST_FLAGS` around the line for extra
  compiler flags (the libk malloc test uses it).
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
| `libk_malloc_test.c` | `user/libk/malloc.c` (built with `LIBK_HOST_TEST`, which renames it `libk_malloc` and so on): alignment, neighbour separation, coalescing, pool growth and release, direct page-run blocks, bad and double frees, `realloc`/`calloc`, exhaustion, a 200 000-round stress with `kheap_info` consistency checks |
| `user_mem_test.c` | `src/proc/user_mem.c`: per-task page-run records behind `alloc_pages`/`free_pages`, foreign and double frees, reclaim on teardown, no leaks on failure |
| `filesystem_test.c` | `src/fs/fat32.c` and `src/fs/namespace.c` on mtools-made volumes: independent geometry/caches, root/BPB labels, duplicate labels/device collisions, relative paths, cwd isolation, retained handles, malformed geometry/chains/LFN, allocation and I/O errors |
| `blkdev_test.c` | `src/fs/blkdev.c`: bare FAT32 and mixed MBR discovery, hidden FAT32 types, invalid/range/overlap tables, partition-relative bounded read/write/flush, read-only and unsupported I/O, registry capacity |
| `kmalloc_test.c` | `src/mm/kmalloc.c`, `src/mm/kmalloc_stress.c`: alignment, zeroing, cache-line separation, block reuse, slab release, page runs, over-aligned blocks, exhaustion, invalid and double frees, balanced IRQ masking, seeded stress |
| `term_test.c` | `src/video/term.c`: autowrap, scrollback and its view, scroll regions, insert/delete/erase, alternate screen, status replies, colours (16/256/24-bit, bce), UTF-8 and DEC line drawing, tabs, origin mode, cursor style/visibility, OSC, REP |

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

./build.sh --target qemu_virt
tests/run-qemu-smoke.py --target qemu_virt --keyboard --disk build/userfs/aarch64/koraos.img --repeat 256
```

The test checks the boot-time kernel heap self-test, boots to the ELF shell,
runs filesystem/argv/exit-code checks plus an EL0 namespace probe (child cwd
inheritance and isolation, failed path handling, and descriptors retained across
chdir), switches to the serial debug console,
verifies timer/UART IRQ progress, runs `heaptest` (seeded kernel heap stress)
and checks that `heap` reports no leaks or bad frees, and checks terminal
colors and graphics pixels using QEMU screenshots. Virt optionally injects
Shift/release, Backspace, Enter and scrollback through a VirtIO keyboard. UART
observes output. `--repeat N` runs `allocprobe` N times: page runs, a `malloc`
heap grown to about 1.5 MiB with churn and `realloc`, a nested child while it is
live, and deliberate leaks. The kernel heap's live count and the free page count
must be the same after the last run as after the first, so everything a reaped
task held, leaked or not, came back.

Use `--release` and `--build-dir` to match the build configuration. `--kernel`
and `--out` override the image and artifact locations. Defaults are the selected
`build/debug/<target>/kernel.img` and its `qemu-smoke/` directory. `--machine`
remains a deprecated alias for target selection. `--no-graphics` omits ramfb on
virt and tests serial fallback. `--disk-writable` negotiates writable test media;
FAT32 itself remains read-only.

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
forces stale BPB labels, and verifies the source SHA256 remains unchanged.

`--expect-root-failure --disk <zeroed-image>` requires a selected VirtIO disk,
failed mount and failed init load, so silent ramdisk fallback cannot pass.

## CI coverage

One producer job publishes `koraos.img` and user ELF symbols under a shared
AArch64 artifact. Consumers download it and use `--userfs-dir` without invoking
userland compilation or filesystem generation. The all-target job builds four
Debug kernels, stages both hardware Release payloads into a temporary directory,
creates a FAT32 SD image, and runs raspi3b smoke. Separate virt profiles cover
embedded graphics, 128 MiB external disk/keyboard, primary MBR boot, two labeled
partitions with namespace navigation, 64 MiB serial-only and EL2 entry. UART/screenshots are uploaded on failure. Pure host sanitizer suites stay
independent of the producer and kernel jobs.

QEMU cannot validate real Pi USB, HDMI, caches or boot firmware. Hardware testing
remains separate. Pi 3 USB IRQ counters include idle frame processing; compare
controller-specific behavior rather than interpreting them as keystroke counts.
