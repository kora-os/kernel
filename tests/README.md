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

## QEMU smoke test (`tests/run-qemu-smoke.py`)

Boots the QEMU kernel on the `raspi3b` machine, drives the shell over the serial
line and checks the results: boot to the shell, `ls`, `hello`, `echo` (argv and
exit code), `cat` of a long-filename file, Ctrl-T to the kernel debug console
and back, and screenshots of `termdemo` (colour tables on screen) and `gfxdemo`
(gradient pixels). It waits for expected output rather than sleeping, so it
does not depend on how fast QEMU runs, and fails fast on a kernel fault banner.

```bash
RPI_VERSION=3 ./build.sh --qemu
tests/run-qemu-smoke.py
```

Needs Python 3 (standard library only) and `qemu-system-aarch64`. Artifacts
land in `build/qemu-smoke/`: `serial.log` and a PNG per screenshot.

It cannot cover what QEMU does not model: USB, real HDMI output, caches,
interrupt timing, or the Pi 4 (QEMU's `raspi4b` machine is incomplete). Those
stay manual hardware tests.

## QEMU virt

`./build.sh --virt --build-dir build-virt` produces an independent image.
Run serial/EL0/FAT32/IRQ and ramfb screenshot checks with:

```bash
tests/run-qemu-smoke.py --machine virt --kernel build-virt/kernel-virt.img --out build-virt/qemu-smoke
```

Use `--ram 128M` to exercise a different memory size. The retained raspi3b
smoke and Pi hardware builds continue to run in CI.

`--no-graphics` omits ramfb and verifies serial fallback. Host ramfb tests
check validation and the fw_cfg configuration format under ASan/UBSan.

External VirtIO rootfs coverage:

```bash
tests/run-qemu-smoke.py --machine virt --kernel build-virt/kernel-virt.img --disk build-virt/fs/koraos.img --out build-virt/qemu-disk
```

The test requires the VirtIO backend boot diagnostic before exercising the
filesystem and EL0 programs. `--disk-writable` negotiates writable media;
FAT32 still makes no writes. Host transport/block suites check features,
queue wraparound, DMA directions, read/write chunking, RO and flush errors,
corrupt completions and retained-buffer lifetime after failure.

Add `--keyboard` to a virt smoke run to attach a VirtIO keyboard and inject
Shift/release, Backspace and Enter through QEMU's monitor, run an EL0 command,
and exercise scrollback keys. The virtual keyboard is the input source for this
step; UART observes output. Pure keymap and input queue/IRQ failure scenarios
also run under host ASan/UBSan. Serial-only profiles still exercise absence.

## Sustained virt CI

Four independent profiles run on PRs and main pushes: embedded graphics,
128 MiB external disk + keyboard, 64 MiB serial, and EL2 entry. Logs and
screenshots are uploaded even when a profile fails. `--repeat N` checks the
64 KiB EL0 heap and nested process fixture N times (256 in the disk profile).
The same job rejects a zeroed configured FAT32 disk using
`--expect-root-failure`; it must report disk selection, failed mount and failed
init load. Hardware and raspi3b build/smoke gates stay enabled separately.
