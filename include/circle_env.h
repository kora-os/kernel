// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// C entry point into the vendored Circle USB stack. Constructs the KoraOS
// HAL-bridge singletons (CInterruptSystem/CTimer/CLogger, backed by KoraOS's
// interrupt controller, generic timer, and console) and the DWC2 USB host
// controller. Step 3b-ii constructs only; enumeration is enabled in 3c.

#ifdef __cplusplus
extern "C" {
#endif

void circle_usb_init(void);

#ifdef __cplusplus
}
#endif
