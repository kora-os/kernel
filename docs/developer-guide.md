# Developer Guide

This guide captures the current build and run workflow for KoraOS. It focuses on the LLVM/Clang toolchain, the CMake-based build system, and how to exercise the kernel under QEMU and on real Raspberry Pi hardware.

## Toolchain Overview

KoraOS is built with the LLVM toolchain while targeting `aarch64-none-elf`. The CMake build system generates `compile_commands.json`, which keeps clangd and other tooling in sync with the cross-compilation flags.

### Required Packages

- LLVM/Clang (including `clang`, `ld.lld`, and `llvm-objcopy`)
- CMake 3.20 or newer
- [mtools](https://www.gnu.org/software/mtools/) (`mformat`, `mcopy`), used to
  build the embedded FAT32 filesystem image; the build fails without it
- QEMU (only for virtualization workflows)
- Optional: GNU Make (the legacy Makefile is still functional)

### Installing the Toolchain

macOS (Homebrew):

```bash
brew install llvm cmake qemu mtools
echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zprofile
```

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install clang lld llvm llvm-objcopy cmake qemu-system-aarch64 mtools
```

After installation, confirm the tools are available:

```bash
clang --version
ld.lld --version
llvm-objcopy --version
```

### Generating `compile_commands.json`

Run `./build.sh` at least once. CMake writes `build/compile_commands.json`, which clangd automatically discovers via the repository's `.clangd` configuration.

## Building for QEMU

The CMake build produces a QEMU-friendly image that enables the PL011 UART (`QEMU_TESTING` is defined).

```bash
# Build both QEMU and hardware variants (default)
./build.sh

# Just the QEMU variant
./build.sh --qemu
```

Useful flags:

- `--release` or `--debug` chooses the CMake build type (default debug).
- `-v all|qemu|hw` mirrors the `--qemu`/`--hw` shortcuts.
- `--build-dir <path>` isolates artifacts in a custom directory.
- Environment variables such as `RPI_VERSION` and `BOOTMNT` can be provided on the command line (for example `RPI_VERSION=4 ./build.sh --qemu`).

Expected outputs (in `build/` by default):

- `kernel8.elf` and `kernel8.img` – ELF and binary images for QEMU.
- `compile_commands.json` – compilation database for clangd and other tooling.
- `boot/kernel8-qemu.img` – QEMU-ready image copied to a local boot staging folder alongside `config.txt`.
- `fs/koraos.img` – the FAT32 filesystem image that is embedded into the kernel (see [filesystem.md](filesystem.md)).

Manual CMake flow:

```bash
mkdir -p build
cmake -S . -B build -DRPI_VERSION=4
cmake --build build -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
```

## Running with QEMU

After building the QEMU variant, launch the emulator:

```bash
./run-qemu.sh
```

The script boots `build/kernel8.img` on the `raspi3b` machine, connects the UART to your terminal, and hides the graphical display. Provide additional QEMU flags by appending them to the command (for example `./run-qemu.sh -s -S` to wait for a debugger).

To also open the framebuffer window, run with `KORA_QEMU_FB=1 ./run-qemu.sh`.

> **Keyboard input goes to the terminal, not the framebuffer window.** The kernel
> reads input only from the serial UART, which QEMU wires to the terminal you
> launched from. The framebuffer window is output-only — there is no USB/HID
> keyboard driver yet — so the shell echoes to both the terminal and the window,
> but you must *type into the terminal*.

Quit QEMU with `Ctrl-A X`. If you need automated smoke tests, pipe input to the script (e.g. `echo "test" | timeout 2 ./run-qemu.sh`).

## Building for Raspberry Pi Hardware

Select the hardware variant to target the Mini UART peripherals and real Raspberry Pi memory map:

```bash
./build.sh --hw --release
```

Artifacts you should see:

- `build/kernel8-hw.elf` and `build/kernel8-hw.img`
- `build/boot/kernel8-rpi4.img` (or the appropriate board suffix)
- `build/kernel8-hw.map`

### Copying to a Boot Volume

Set `BOOTMNT` to the mount point of your SD card's FAT partition and use the install target:

```bash
BOOTMNT=/Volumes/BOOT ./build.sh --hw
cmake --build build --target install_hw
```

`install_hw` copies the hardware image and `config.txt` to `BOOTMNT`. If you build both variants, `cmake --build build --target install_kernel` pushes every enabled image.

### Verifying the Image

List the files on the boot volume to ensure `kernel8-hw.img` is present, eject the volume safely, and boot the Raspberry Pi. The kernel prints `K` on the serial console, followed by its boot log.

On hardware the serial console and the user console are separate. The UART carries the kernel's diagnostic log (`printf`, `console_log`); the shell runs on the HDMI screen and reads the USB keyboard: through the DWC2 controller on the Pi 3, and through the VL805 USB 3 (xHCI) controller behind PCIe on the Pi 4, on any of its four type-A ports (USB 2 or USB 3), directly or through a hub.

USB limitations: devices are enumerated once at boot, so plug the keyboard (or a hub with the keyboard) in before powering on; there is no hot-plug yet. On the Pi 4 only the four type-A ports are supported (the VL805 xHCI controller); the USB-C port is a different controller (DWC2) that the Pi 4 build does not include, and it normally powers the board anyway. On the Pi 3 a keyboard behind an extra hub does not work yet: low/full-speed devices behind a High Speed hub need split transactions, which the DWC2 has to schedule in software (the Pi 4's xHCI does this in hardware). Keyboard auto-repeat is not enabled yet.

Typing on the serial line talks to the kernel debug console (`koraos> `, `src/console.c`), which runs alongside the shell: it is fed from the UART interrupt and from the shell's idle loop, not a task of its own, so its commands must not block. Press **Ctrl-T** to hand the serial line to the screen terminal as a fallback keyboard (what you type is then echoed on the screen, not on the serial line), and Ctrl-T again to return. Besides `help` and `version`, `irqs` shows the interrupt controller in use (the legacy BCM controller on the Pi 3, the GIC-400 on the Pi 4), the system tick count next to the uptime (about 100 ticks per second when interrupts work), and how often each connected IRQ has fired.

Under QEMU, which has no USB keyboard, the serial line starts on the screen terminal and the shell is mirrored to the UART as before; Ctrl-T reaches the debug console. The keyboard layout defaults to US; pick another of Circle's keymaps at configure time with `-DKORAOS_KEYMAP=IT` (or UK, DE, FR, ES, DV).

The screen terminal understands the xterm escape sequences that the `xterm-256color` terminfo entry uses: cursor movement and addressing, erase/insert/delete, scroll regions, SGR attributes with 16, 256 and 24-bit colour, the alternate screen, save/restore cursor, tab stops, DEC line drawing (and UTF-8 box characters), and the status/size reports. `termdemo` shows most of it. **Shift+PgUp / Shift+PgDn** scroll back through the history (`TERM_SCROLLBACK_LINES`, default 1000); new output returns to the live screen. The cursor is a block by default; set `TERM_DEFAULT_CURSOR` in `include/video/term.h` to `TERM_CURSOR_UNDERLINE` or `TERM_CURSOR_BAR` (programs can also change it with `ESC [ n SP q`).

## Tests

Pure-logic kernel code has host-side unit tests that run on your machine under AddressSanitizer/UndefinedBehaviorSanitizer, with no QEMU or hardware:

```bash
tests/run-host-tests.sh
```

A QEMU smoke test boots the kernel on `raspi3b` and drives the shell over serial, checking the output and framebuffer screenshots:

```bash
RPI_VERSION=3 ./build.sh --qemu
tests/run-qemu-smoke.py
```

See `tests/README.md` for what each covers and how to add tests.

## Legacy Makefile

`make` still produces a bootable kernel, but the CMake flow is the source of truth and the only way to refresh `compile_commands.json`. Use Make only if you need compatibility with existing tooling.

## Further Documentation

- [filesystem.md](filesystem.md) – the read-only FAT32 filesystem, the ramdisk, and how the image is built and embedded.
- [how-userland-works.md](how-userland-works.md) – what happens when a program is loaded, where it lives in memory, how processes coexist, and the (deliberate) lack of memory protection.
- [syscalls.md](syscalls.md) – the full system-call ABI.
- [writing-userland-programs.md](writing-userland-programs.md) – how to write, build, and run a userland program (no compiler or libc on the device yet).

