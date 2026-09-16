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

List the files on the boot volume to ensure `kernel8-hw.img` is present, eject the volume safely, and boot the Raspberry Pi. The kernel prints `K` on the serial console and echoes characters received over UART.

## Legacy Makefile

`make` still produces a bootable kernel, but the CMake flow is the source of truth and the only way to refresh `compile_commands.json`. Use Make only if you need compatibility with existing tooling.

## Further Documentation

- [filesystem.md](filesystem.md) – the read-only FAT32 filesystem, the ramdisk, and how the image is built and embedded.
- [how-userland-works.md](how-userland-works.md) – what happens when a program is loaded, where it lives in memory, how processes coexist, and the (deliberate) lack of memory protection.
- [syscalls.md](syscalls.md) – the full system-call ABI.
- [writing-userland-programs.md](writing-userland-programs.md) – how to write, build, and run a userland program (no compiler or libc on the device yet).

