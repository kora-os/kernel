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
  controller (Raspberry Pi 3 and the Pi 4 USB-C port) and the VL805 xHCI
  controller behind PCIe (Raspberry Pi 4 type-A ports), plus the HID keyboard
  function driver.
- **How we track it:** rather than importing from upstream directly, we vendor
  from our own pinned fork at **kora-os/circle** so a specific, known-good commit
  is recorded and an upstream change cannot silently break our build. The vendored
  tree and its exact commit are noted where the code lives (`third_party/circle/`).

USPi (<https://github.com/rsta2/uspi>), the C USB library carved out of Circle,
was evaluated first. We chose Circle because, unlike USPi, it supports the
Raspberry Pi 4's xHCI controller, letting a single stack serve both boards.

## The GPLv3 choice

KoraOS is GPLv3-or-later on purpose: it is an open experiment in the spirit of
home computers you can read and reshape, not a product. GPLv3 also lets us
incorporate GPLv3, GPLv2-or-later, and permissively licensed (MIT/BSD/ISC) code,
which keeps well-tested bare-metal drivers within reach as the project grows.
