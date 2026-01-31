# Agent Handbook

Use this file when you drop into the workspace so you can ramp quickly and keep changes aligned with project conventions.

## Project Snapshot
- **Target**: Bare-metal Raspberry Pi kernel (Pi 3 via QEMU raspi3b, Pi 4 hardware)
- **Current Capabilities**: Boot flow and UART console bring-up verified on QEMU and real hardware
- **Toolchain**: LLVM/Clang cross-compilation managed by CMake (`./build.sh`), legacy Makefile retained for compatibility

## Where To Look
- `README.md` – entry point with quick-start build/run commands.
- `docs/developer-guide.md` – detailed toolchain setup, build variants, QEMU usage, hardware deployment.
- `docs/repository-guidelines.md` – coding style, directory layout, and contribution workflow.
- `project_doc/roadmap.md` – high-level milestones for upcoming kernel features (exception vectors, interrupts, console polish).

## Working Guidelines
- Follow the clang-flavored C style already in the tree (four-space indent, same-line braces, snake_case identifiers).
- Keep headers self-contained; prefer `const` pointers for memory-mapped peripherals.
- Build via `./build.sh` to refresh `build/compile_commands.json` for clangd.
- Use `run-qemu.sh` for rapid UART smoke tests; see the developer guide for hardware installation steps.
- Documentation lives under `docs/`; do not store temporary todos there—use `project_doc/` for roadmap-level planning instead.

## Housekeeping
- Generated artifacts land in `build/`; wipe safely with `rm -rf build` when needed.
- SD card images can be staged or created with `create-sd-image.sh` once the hardware kernel variant exists.
- Keep ARM system register definitions, exception vectors, and interrupt bring-up work in sync with the roadmap before adding new subsystems.

If you uncover missing instructions or new best practices, update this handbook alongside the relevant docs so future agents stay aligned.
