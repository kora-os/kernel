// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// C entry point into the vendored Circle USB stack. Constructs the KoraOS
// HAL-bridge singletons (CInterruptSystem/CTimer/CLogger, backed by KoraOS's
// interrupt controller, generic timer, and console), Circle's CMachineInfo, and
// the board's USB host controller (DWC2 on the Pi 3, xHCI on the Pi 4).

#ifdef __cplusplus
extern "C" {
#endif

// Construct the Circle USB stack on the KoraOS HAL bridge. When `enumerate` is
// nonzero, also initialize the host controller and enumerate devices, then feed
// an attached USB keyboard into the tty. Enumeration talks to real USB
// hardware, so callers pass 0 under QEMU and 1 on a real Raspberry Pi.
void circle_usb_init(int enumerate);

#ifdef __cplusplus
}
#endif
