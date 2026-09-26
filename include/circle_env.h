// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// C entry point into the vendored Circle USB stack. Constructs the KoraOS
// HAL-bridge singletons (CInterruptSystem/CTimer/CLogger, backed by KoraOS's
// interrupt controller, generic timer, and console) and the DWC2 USB host
// controller. Step 3b-ii constructs only; enumeration is enabled in 3c.

#ifdef __cplusplus
extern "C" {
#endif

// Construct the Circle USB stack on the KoraOS HAL bridge. When `enumerate` is
// nonzero, also initialize the DWC2 controller and enumerate devices, then log
// keystrokes from an attached USB keyboard over the console. Enumeration talks
// to real USB hardware, so callers pass 0 under QEMU (raspi3b has no USB) and 1
// on a real Raspberry Pi.
void circle_usb_init(int enumerate);

#ifdef __cplusplus
}
#endif
