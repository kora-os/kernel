# Credits and Third-Party Code

KoraOS is free software licensed under the GNU General Public License, version 3
or (at your option) any later version. See [`LICENSE`](LICENSE) for the full
text.

KoraOS stands on the shoulders of other bare-metal Raspberry Pi work. This file
records code we incorporate or derive from, and the people who wrote it. Each
imported file additionally keeps its original license header; this list is the
project-level summary, not a replacement for those headers.

## Circle

- **Project:** Circle, a C++ bare-metal environment for Raspberry Pi.
- **Author:** Rene Stange and contributors.
- **Upstream:** <https://github.com/rsta2/circle>
- **License:** GNU General Public License, version 3 or later (GPL-3.0-or-later).
- **What we use:** Circle's USB stack, which drives the Synopsys DWC2 OTG
  controller (Raspberry Pi 3) and the VL805 xHCI controller behind PCIe
  (Raspberry Pi 4 type-A ports), plus the HID keyboard function driver, the
  BCM2711 PCIe host bridge, and the device-tree reader.
- **How we track it:** rather than importing from upstream directly, we vendor
  from our own pinned fork at **kora-os/circle** so a specific, known-good commit
  is recorded and an upstream change cannot silently break our build. The vendored
  tree and its exact commit are noted where the code lives (`third_party/circle/`).

USPi (<https://github.com/rsta2/uspi>), the C USB library carved out of Circle,
was evaluated first. We chose Circle because, unlike USPi, it supports the
Raspberry Pi 4's xHCI controller, letting a single stack serve both boards.

## musl libm

- **Project:** musl, a C standard library.
- **Author:** Rich Felker and contributors; much of the math code is by Sun
  Microsystems, the FreeBSD project's contributors and Arm Limited, as the
  individual files say.
- **Upstream:** <https://musl.libc.org/>
- **License:** MIT (`user/libc/src/math/COPYRIGHT`).
- **What we use:** the `<math.h>` functions for `double` and `float`, copied
  from musl 1.2.5 into `user/libc/src/math`. The rest of the C library is
  KoraOS's own. `user/libc/src/math/README.md` lists the files and every local
  change.

## Spleen

- **Project:** Spleen, monospaced bitmap fonts.
- **Author:** Frederic Cambus.
- **Upstream:** <https://github.com/fcambus/spleen>
- **License:** BSD 2-Clause (`third_party/spleen/LICENSE`).
- **What we use:** the BDF sources of the 5x8, 6x12, 8x16, 12x24 and 16x32
  sizes of release 2.3.0, in `third_party/spleen` (`PROVENANCE.md` there has
  the exact commit). The build converts them to PSF2 for the consoles'
  `font` command.

## Raspberry Pi boot firmware

- **Files:** the GPU boot firmware and device trees in [`firmware/`](firmware/)
  (`bootcode.bin`, `start*.elf`, `fixup*.dat`, `*.dtb`).
- **Source:** <https://github.com/raspberrypi/firmware> (pinned; see
  `firmware/README.md`).
- **License:** Raspberry Pi / Broadcom firmware license (`firmware/LICENCE.broadcom`).
- These are loaded by the SoC before KoraOS and are not linked into it (mere
  aggregation), so they do not affect KoraOS's licensing.

## The GPLv3 choice

KoraOS is GPLv3-or-later on purpose: it is an open experiment in the spirit of
home computers you can read and reshape, not a product. GPLv3 also lets us
incorporate GPLv3, GPLv2-or-later, and permissively licensed (MIT/BSD/ISC) code,
which keeps well-tested bare-metal drivers within reach as the project grows.
