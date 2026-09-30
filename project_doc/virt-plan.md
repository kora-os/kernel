# QEMU virt implementation ledger

## Outcome and scope

Add an independent AArch64 QEMU virt target for kernel and EL0 development,
retaining QEMU raspi3b and Raspberry Pi 3/4 hardware builds. The initial profile
is cortex-a72, GICv2, one CPU, TCG, RAM below 4 GiB. Discover RAM, PL011, GIC,
fw_cfg and VirtIO MMIO from the generated device tree. Preserve the existing
flat identity mapping and cooperative process model.

## PR milestones

- [x] 1. Platform boot: build/launcher selection, DTB discovery, linker/entry,
  MMU/RAM reservations, PL011, GICv2 and generic timer; embedded-rootfs EL0 shell.
- [x] 2. Display: fw_cfg DMA and ramfb through the existing framebuffer API;
  serial fallback and terminal/graphics screenshots.
- [x] 3. Storage: modern VirtIO MMIO split queues and block I/O beneath FAT32;
  external disk boot, bounded error paths, embedded-rootfs fallback.
- [x] 4. Input: VirtIO keyboard, modifiers and terminal editing/scrollback;
  monitor-injected keys and serial fallback.
- [x] 5. CI/docs: virt regression profiles on PRs and main pushes, artifacts,
  allocator/process repetition, retained raspi3b/Pi builds and usage docs.

## Delivery workflow

Work in this separate checkout on branches pushed only to lazzelor/koraos-kernel.
For each milestone, commit, open a cross-fork PR against kora-os/kernel main,
independently review the resulting diff, run appropriate local tests and wait
for successful GitHub checks. Fix findings before merging. Approve when GitHub
permits it (an author cannot approve their own PR); record independent review
otherwise. Merge only successful milestones, then fast-forward/synchronize the
fork main and start the next branch from upstream. Never force unrelated work.

## Verification

Use host ASan/UBSan tests for parsers and queue logic; build all retained target
variants; boot virt with serial shell, ELF commands, FAT32 files, IRQ tick
progress, different RAM sizes and EL entry modes; validate ramfb pixels with
QEMU screendump; read external disk with the same filesystem/userland tests;
inject keyboard events. Save UART/screenshots under build/. Real Pi boot/USB/
HDMI are unverified unless physical hardware is available.

## References

- https://www.qemu.org/docs/master/system/arm/virt.html
- https://www.qemu.org/docs/master/specs/fw_cfg.html
- https://github.com/qemu/qemu/blob/master/hw/display/ramfb.c
- https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html

### Milestone 1 validation

Host ASan/UBSan: 1189 DTB parser, 68 terminal, 20 printf checks pass. Actual
QEMU DTB parses; virt serial smoke passes at 256/128 MiB with ELF programs,
FAT32 and nonzero timer/UART IRQs. raspi3b full graphics smoke and Pi3/Pi4
release builds pass. Use raw Image boot; ELF remains the symbol artifact.
Independent review fixes include edge IRQ metadata, unrelated PCI cell widths,
RAM/DTB validation and all reserved-memory tuples. Physical Pi not tested.

### Milestone 2 validation

ramfb host tests: 98 sanitizer checks. Actual QEMU ramfb terminal/graphics
smoke passes (RGB pixel samples and 256/24-bit colors), and absent-ramfb serial
smoke passes. ELF pixels symbol is 0x40e00000, 2 MiB aligned. Independent review
found no blocking defects; DMA buffers remain permanently reserved on timeout.
Milestone 1 upstream PR #38 merged; fork main synchronized.

### Milestone 3 validation

Modern VirtIO disk boot passed with read-only and writable media, including
EL0/FAT32/ramfb checks; missing disk ramdisk/serial fallback passed. Transport
116-check and block 5219-check suites plus error scenarios pass under
ASan/UBSan. Independent review fixed failed-device bounce mutation; regression
checks retain request and DMA storage untouched after failure. Milestone 2
upstream PR #39 merged; fork main synchronized.

### Milestone 4 validation

Independent review passed. 238 host sanitizer checks cover modifiers/releases,
key translation, IRQ framing, and failed setup. Actual QEMU monitor key injection
runs `echo Ab` through the keyboard with Shift release, Backspace and Enter,
then exercises scrollback and the full external-rootfs/ramfb smoke. Milestone 3
upstream PR #40 merged; fork main synchronized.

### Milestone 5 validation

All four final virt profiles passed locally: embedded graphics (64 allocation
probes), 128 MiB external disk/keyboard/graphics (256 probes), serial-only
64 MiB (32 probes), and EL2 entry (32 probes). Invalid configured disk failed
without fallback. All host sanitizer suites passed. Clean default and combined
three-family builds passed, as did Pi 3/4 builds and raspi3b full smoke. Target
selection now keeps raspi3b on Pi 3 independently of the hardware board;
separate Circle libraries, shared rootfs dependency and LLVM archives fix
combined builds. Independent review found no blocking findings. Milestone 4
upstream PR #41 merged and fork main synchronized before this final PR.

Merged implementation PRs: [boot #38](https://github.com/kora-os/kernel/pull/38),
[ramfb #39](https://github.com/kora-os/kernel/pull/39),
[storage #40](https://github.com/kora-os/kernel/pull/40),
[keyboard #41](https://github.com/kora-os/kernel/pull/41).
