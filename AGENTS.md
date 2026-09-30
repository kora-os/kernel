# Agent Handbook

Use this file when you drop into the workspace so you can ramp quickly and keep changes aligned with project conventions.

## Project Snapshot
- **Target**: QEMU AArch64 virt (independent development target), plus Bare-metal Raspberry Pi kernel (Pi 3 via QEMU raspi3b, Pi 4 hardware)
- **Current Capabilities**: EL0/FAT32 console, Pi USB keyboards; virt PL011/GICv2, ramfb, VirtIO storage/input and CI development profiles
- **Toolchain**: LLVM/Clang cross-compilation managed by CMake (`./build.sh`)

## Where To Look
- `README.md` – entry point with quick-start build/run commands.
- `docs/developer-guide.md` – detailed toolchain setup, build variants, QEMU usage, hardware deployment.
- `docs/repository-guidelines.md` – coding style, directory layout, and contribution workflow.
- `project_doc/virt-plan.md` – virt target milestones and delivery verification.
- `project_doc/roadmap.md` – high-level milestones for upcoming kernel features (exception vectors, interrupts, console polish).

## Working Guidelines
- Follow the clang-flavored C style already in the tree (four-space indent, same-line braces, snake_case identifiers).
- Keep headers self-contained; prefer `const` pointers for memory-mapped peripherals.
- Build with `./build.sh --target <name>`; repeated targets select a subset, and
  `all` selects qemu_raspi3b, qemu_virt, hw_raspi3b and hw_raspi4b. The first
  selected kernel supplies `build/compile_commands.json` for clangd.
- Use `build.sh --target qemu_virt` and `run-qemu.sh --target qemu_virt` for
  independent development; raspi3b is `qemu_raspi3b`. User programs/FS generation
  live in `cmake/userfs.cmake` and `cmake/userfs`; kernels consume shared userfs.
- Use `run-qemu.sh` for rapid UART smoke tests; see the developer guide for hardware installation steps.
- Documentation lives under `docs/`; do not store temporary todos there—use `project_doc/` for roadmap-level planning instead.

## Housekeeping
- Kernel artifacts use `build/<config>/<target>/kernel.{elf,img,map}`; userfs
  uses `build/userfs/aarch64`. Script cleanup preserves unselected targets.
- Hardware installation requires explicit `--install-to`; retain firmware boot
  filenames. Prepared external userfs is never cleaned by the orchestrator.
- SD card images can be staged or created with `create-sd-image.sh` once the hardware kernel variant exists.
- Keep ARM system register definitions, exception vectors, and interrupt bring-up work in sync with the roadmap before adding new subsystems.

If you uncover missing instructions or new best practices, update this handbook alongside the relevant docs so future agents stay aligned.
