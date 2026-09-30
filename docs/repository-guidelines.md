# Repository Guidelines

## Project Structure & Module Organization
The bare-metal kernel lives under `src/`, mixing C (`kernel.c`, `gpio.c`, `memory_access.c`) with short assembly stubs (`boot.S`, `mm.S`, `utils.S`). Shared interfaces are in `include/` with peripheral-specific headers in `include/peripherals/`. Device tree blobs and boot assets sit in `firmware/`. Build helpers live in `cmake/` and scripts at the repository root; the `build/` directory is generated and may be safely removed between builds.

## Build, Test, and Development Commands
Run `./build.sh --target qemu_virt` or select any subset with repeated `--target`.
`--target all` builds four configurations. Each kernel owns a cache under
`build/<debug|release>/<target>`; shared AArch64 userfs lives under
`build/userfs/aarch64`. The first selected kernel supplies the build-root
`compile_commands.json` for clangd. Use `--install-to` for explicit hardware
installation. See `developer-guide.md` for supported targets and migration.

## Coding Style & Naming Conventions
Follow the existing clang-flavored C style: four-space indentation, braces on the same line as declarations, and `snake_case` for functions and variables (`gpio_pin_set_func`). Keep headers self-contained and prefer `const` pointers for register blocks defined in `include/`. Preprocessor constants remain uppercase (`RPI_VERSION`, `QEMU_TESTING`). When in doubt, mirror the patterns in `src/gpio.c` and `include/common.h`.

## Testing Guidelines
Run `tests/run-host-tests.sh` and the appropriate QEMU smoke profile (see
`tests/README.md`). CI runs these on PRs and main pushes. Smoke-test changes by booting the selected configuration’s `kernel.img` on the intended hardware or via your QEMU setup. Use `--target hw_raspi3b --release --install-to /Volumes/BOOT` to copy a
complete hardware payload to a mounted volume. Include any UART logs or observed regressions in your PR.

## Commit & Pull Request Guidelines
Commits follow short, imperative subjects (`Use CMakefile instead of Makefile`). Group related changes and avoid work-in-progress checkpoints. PRs should explain the motivation, list user-visible effects, note the Raspberry Pi model exercised, and attach screenshots or UART output if behavior changes. Link issues when applicable and call out configuration defaults you touched.

## Hardware & Deployment Tips
Keep device tree blobs in `firmware/` synchronized with the Raspberry Pi revision you target. Long-lived branches should re-run `./build.sh` so clangd picks up new compilation flags. When changing memory maps or linker scripts, double-check `src/linker.ld` and document the update in your PR description.

