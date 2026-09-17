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

## What was NOT vendored

Circle's HAL that KoraOS replaces with its own is **not** used: Circle's boot,
MMU, exception handling, memory manager, interrupt controller, timer, logger,
and scheduler. KoraOS bridges its own interrupt controller, generic timer, frame
allocator, and console to Circle's `CInterruptSystem` / `CTimer` / memory /
`CLogger` interfaces via adapters in `src/circle/` (added in step 3b-ii). Also
excluded: the Pi 4 xHCI/PCIe path (added with Pi 4 bring-up), gadget mode, and
the net/sound/graphics/filesystem/scheduler subsystems.

## Updating the pin

Re-vendor from a new `kora-os/circle` commit by re-running the closure and copy,
then update the pinned commit above. Do not edit vendored files in place except
where a change is unavoidable; record any such change here.
