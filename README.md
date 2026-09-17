# KoraOS

KoraOS is a bare-metal Raspberry Pi kernel written in C with a small assembly shim. The project targets the aarch64 architecture and uses LLVM/Clang together with CMake for a modern cross-compilation workflow. It boots in QEMU (raspi3b) and on Raspberry Pi 4 hardware, drops to EL0, and runs userland programs (an interactive shell and utilities) loaded from an embedded read-only FAT32 filesystem.

**New here? Read the [Manifesto](MANIFESTO.md)** for what KoraOS is and why it exists: a home computer for the user/developer, in the spirit of the machines you could `PEEK` and `POKE` to your heart's content.

## Quick Start

```bash
# Build QEMU and hardware variants (creates build/compile_commands.json)
./build.sh

# Boot the QEMU image
./run-qemu.sh
```

You need LLVM/Clang, CMake 3.20+, [mtools](https://www.gnu.org/software/mtools/) (to build the embedded filesystem image), and (optionally) QEMU installed on your workstation. The build script produces `build/kernel8.img` for virtualization and `build/kernel8-hw.img` when the hardware variant is enabled. Re-run `./build.sh` whenever you change compiler flags so clangd receives updated metadata via `build/compile_commands.json`.

## Building for Hardware

```bash
BOOTMNT=/Volumes/BOOT ./build.sh --hw --release
cmake --build build --target install_hw
```

This generates a Mini-UART-aware image and copies it, along with `config.txt`, to your mounted SD card partition (`BOOTMNT`). Safely eject the volume before inserting it into the Raspberry Pi.

## Documentation

- [`MANIFESTO.md`](MANIFESTO.md) – What KoraOS is and why it exists.
- `docs/developer-guide.md` – Toolchain requirements, build configurations, QEMU workflow, and Raspberry Pi deployment instructions.
- `docs/filesystem.md` – The read-only FAT32 filesystem, the embedded ramdisk, and how the image is built.
- `docs/how-userland-works.md` – How a program is loaded and placed in memory, how processes coexist, and the (deliberate) lack of memory protection.
- `docs/syscalls.md` – The system-call ABI.
- `docs/writing-userland-programs.md` – How to write, build, and run a userland program.

Keep documentation in `docs/` current as the project evolves. Avoid placing temporary notes or todo lists there; use issue trackers or other channels for work-in-progress planning.

## License

KoraOS is free software: you can redistribute it and/or modify it under the terms
of the GNU General Public License as published by the Free Software Foundation,
either version 3 of the License, or (at your option) any later version. See
[`LICENSE`](LICENSE) for the full text.

Third-party code we incorporate (notably the [Circle](https://github.com/rsta2/circle)
USB stack) and the people who wrote it are credited in [`CREDITS.md`](CREDITS.md).
Each imported file also keeps its own original license header.
