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
- [ ] 3. Storage: modern VirtIO MMIO split queues and block I/O beneath FAT32;
  external disk boot, bounded error paths, embedded-rootfs fallback.
- [ ] 4. Input: VirtIO keyboard, modifiers and terminal editing/scrollback;
  monitor-injected keys and serial fallback.
- [ ] 5. CI/docs: virt regression profiles on PRs and main pushes, artifacts,
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
