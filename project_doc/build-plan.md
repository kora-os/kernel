# Uniform targets and shared userfs PR

## Agreed interface

- Repeatable `build.sh --target` accepts `hw_raspi3b`, `hw_raspi4b`,
  `qemu_raspi3b`, `qemu_virt`, and `all`; default is all four.
- Kernel caches/artifacts live in `build/<debug|release>/<target>/` with
  `kernel.elf`, `kernel.img`, `kernel.map`; `--build-dir` changes the build root.
- A standalone userfs producer builds shared AArch64 PIE programs in
  `build/userfs/aarch64/user/` and `build/userfs/aarch64/koraos.img`. Current
  user flags remain identical across kernel Debug/Release modes.
- `--userfs-only` builds the producer only. `--userfs-dir DIR` consumes an
  explicitly prepared userfs (used by CI artifact consumers) without rebuilding.
- Default orchestration builds userfs before kernels. Kernel configurations
  consume that immutable image and relink when its contents change.
- `--install-to DIR` explicitly installs selected hardware kernels with existing
  `kernel8-rpi3.img` / `kernel8-rpi4.img`, firmware and config; no implicit install.
- Retain old build/launcher flags as deprecated aliases. Modern targets do not
  require RPI_VERSION or BOOTMNT. Standard CMake hardware install targets remain.
- Launcher and smoke tests use the canonical QEMU target names and matching
  build root/configuration paths. Root compile_commands.json selects the first
  requested kernel target for clangd; document that choice.
- Shared generation must be serialized and publish the FAT32 image atomically.
- CI builds userfs once per workflow, shares its artifact, builds all four
  kernels, retains host and QEMU tests/profiles, and packages both hardware boot
  files without copying QEMU artifacts to the SD payload.

## Ledger

- [x] Extract userfs producer; make kernel CMake consume shared image.
- [x] Implement target orchestration, installation and launcher/SD paths.
- [x] Add meaningful CLI/dependency/install tests; update smoke and CI.
- [x] Update current usage docs and handbook.
- [x] Build all targets/configurations; check shared/incremental behavior and QEMU.
- [x] Independent review and fixes; commit/push to fork; open upstream PR.

Leave the PR unmerged for the user's local hardware tests. This request
supersedes the previous automatic-merge workflow for this PR.

## Verification before PR

- All four canonical Debug targets build through the orchestrator; correct
  per-board compile flags and an unchanged shared image on an incremental run.
- Both Release hardware targets build/install to a temporary payload; FAT32 SD
  image contains both firmware kernel names, config, firmware, DTBs and licence.
- raspi3b full smoke; virt shared external disk/keyboard/graphics smoke and 32
  allocation probes; EL2 and small-RAM serial profiles; host sanitizer suites.
- CLI/lock/SD failure-gate regressions and isolated real-producer invalidation
  tests pass. Removed registered programs no longer ship stale host ELF files.
- Independent review fixes include module/program-manifest dependencies, lock
  lifetime under interruption, prepared-consumer serialization and directory
  output rejection. No blocking findings remain.

CI now builds shared userfs once per workflow and distributes only its image
and ELF symbols to consumers. No physical SD card was written in this session.
The upstream PR remains open for user hardware testing.
