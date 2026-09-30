# Tests

## Host unit tests (`tests/host/`)

Pure-logic kernel code (code that does not touch hardware registers) is compiled
with the **host** compiler and run directly on your machine, under
AddressSanitizer and UndefinedBehaviorSanitizer. No cross toolchain, QEMU or
Raspberry Pi is needed, and a run takes a few seconds.

```bash
tests/run-host-tests.sh
```

Requires `clang` (the default `CC`; Apple's clang works). GCC is not supported:
it rejects the kernel's own `strlen` declaration under `-Werror`.

### Writing a test

- Add `tests/host/<name>_test.c` and a `run_test <name> <kernel sources...>` line
  in `tests/run-host-tests.sh`.
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
| `term_test.c` | `src/video/term.c`: autowrap, scrollback and its view, scroll regions, insert/delete/erase, alternate screen, status replies, colours (16/256/24-bit, bce), UTF-8 and DEC line drawing, tabs, origin mode, cursor style/visibility, OSC, REP |

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

The test boots to the ELF shell, runs filesystem/argv/exit-code checks, switches
the serial debug console, verifies timer/UART IRQ progress, and checks terminal
colors and graphics pixels using QEMU screenshots. Virt optionally injects
Shift/release, Backspace, Enter and scrollback through a VirtIO keyboard. UART
observes output. `--repeat N` checks a 64 KiB EL0 heap across a nested process,
then reaping, N times; 256 runs exceed the pool’s cumulative heap capacity.

Use `--release` and `--build-dir` to match the build configuration. `--kernel`
and `--out` override the image and artifact locations. Defaults are the selected
`build/debug/<target>/kernel.img` and its `qemu-smoke/` directory. `--machine`
remains a deprecated alias for target selection. `--no-graphics` omits ramfb on
virt and tests serial fallback. `--disk-writable` negotiates writable test media;
FAT32 itself remains read-only.

`--expect-root-failure --disk <zeroed-image>` requires a selected VirtIO disk,
failed mount and failed init load, so silent ramdisk fallback cannot pass.

## CI coverage

One producer job publishes `koraos.img` and user ELF symbols under a shared
AArch64 artifact. Consumers download it and use `--userfs-dir` without invoking
userland compilation or filesystem generation. The all-target job builds four
Debug kernels, stages both hardware Release payloads into a temporary directory,
creates a FAT32 SD image, and runs raspi3b smoke. Separate virt profiles cover
embedded graphics, 128 MiB external disk/keyboard, 64 MiB serial-only and EL2
entry. UART/screenshots are uploaded on failure. Pure host sanitizer suites stay
independent of the producer and kernel jobs.

QEMU cannot validate real Pi USB, HDMI, caches or boot firmware. Hardware testing
remains separate. Pi 3 USB IRQ counters include idle frame processing; compare
controller-specific behavior rather than interpreting them as keystroke counts.
