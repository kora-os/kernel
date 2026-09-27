# Vendored: Circle USB stack

This directory holds a **subset** of [Circle](https://github.com/rsta2/circle),
Rene Stange's C++ bare-metal environment for the Raspberry Pi, vendored into
KoraOS to provide the USB host stack (see the project [CREDITS](../../CREDITS.md)).

- **License:** GNU GPL v3 or later (see `LICENSE`), compatible with KoraOS's
  GPL-3.0-or-later. Original per-file copyright headers are preserved.
- **Upstream:** https://github.com/rsta2/circle
- **Tracked via our pinned fork:** https://github.com/kora-os/circle
- **Pinned commit:** `6177984e30fac5e65582d171d43f1563368a94ac`

## What was vendored

Only the files needed for a Raspberry Pi 3 (DWC2) USB HID keyboard, computed as
the `#include` closure of the DWC2 host controller + USB core + standard hub +
HID/keyboard drivers + their leaf utilities:

- `lib/usb/` — DWC2 host controller (`dwhci*`), USB core (`usbdevice`,
  `usbendpoint`, `usbrequest`, `usbfunction`, `usbhostcontroller`,
  `usbconfigparser`, `usbdevicefactory`, `usbstring`, `usbsubsystem`), and the
  device drivers `usbstandardhub`, `usbhiddevice`, `usbkeyboard`, `usbmouse`.
- `lib/` — leaf utilities and the device model: `string`, `ptrlist`,
  `ptrlistfiq`, `numberpool`, `classallocator`, `device`, `devicenameservice`,
  `machineinfo`, `bcmmailbox`, `bcmpropertytags`, `koptions`, `time`,
  `synchronize`, `synchronize64`, `spinlock`.
- `lib/input/` — cooked keyboard input: `keymap`, `keyboardbehaviour`,
  `keyboardbuffer`.
- `include/circle/` — the header closure of the above.

Added for the Raspberry Pi 4 (VL805 xHCI behind PCIe), again as the `#include`
closure, which only needed two new headers (`bcm2711.h`, `pci_regs.h`):

- `lib/usb/` — the xHCI host controller (`xhci*`: device, MMIO space, rings,
  event/command/slot managers, endpoints, root hub and ports, USB device, shared
  memory allocator).
- `lib/` — `bcmpciehostbridge` (the BCM2711 PCIe root complex) and
  `devicetreeblob` (reads the firmware's device tree; `CMachineInfo` takes the
  PCIe DMA window from it).

## What was NOT vendored

Circle's HAL that KoraOS replaces with its own is **not** used: Circle's boot,
MMU, exception handling, memory manager, interrupt controller, timer, logger,
and scheduler. KoraOS bridges its own interrupt controller, generic timer, frame
allocator, and console to Circle's `CInterruptSystem` / `CTimer` / memory /
`CLogger` interfaces via adapters in `src/circle/` (added in step 3b-ii). Also
excluded: gadget mode and the net/sound/graphics/filesystem/scheduler
subsystems.

## Build configuration

The `circle_usb` CMake library compiles this tree together with the KoraOS
bridge adapters in `src/circle/` (`-DAARCH=64 -DRASPPI=<board> -DSTDLIB_SUPPORT=0`,
where the board is `RPI_VERSION`). Circle's USB core is built for one host
controller family, so the Pi 3 build leaves out the xHCI/PCIe/device-tree
sources and the Pi 4 build leaves out the DWC2 (`dwhci*`) sources.
Circle's USB device factory is built keyboard-only via `-DEXCLUDE_USB_*` for
every other class (mouse, gamepads, storage, audio, net, serial, printer,
bluetooth, midi, touchscreen), so their drivers need not be vendored. No
vendored source file is edited in place.

## Updating the pin

Re-vendor from a new `kora-os/circle` commit by re-running the closure and copy,
then update the pinned commit above. Do not edit vendored files in place except
where a change is unavoidable; record any such change here.
