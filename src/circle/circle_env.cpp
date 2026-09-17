// SPDX-License-Identifier: GPL-3.0-or-later
//
// Construction of the vendored Circle USB stack on top of the KoraOS bridge.
// Step 3b-ii constructs the singletons and the DWC2 host controller and proves
// it links and runs without faulting; Initialize()/enumeration is enabled in the
// Pi 3 bring-up (3c), which needs real hardware (QEMU raspi3b has no USB).

#include "circle_env.h"

#include <circle/devicenameservice.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/usb/usbhcidevice.h>

extern "C" void tfp_printf(const char *fmt, ...);

void circle_usb_init(void) {
    // Order matters: name service and interrupt system first, then logger and
    // timer, then the USB host controller that depends on them. Constructed once
    // with static storage duration.
    static CDeviceNameService DeviceNameService;
    static CInterruptSystem InterruptSystem;
    InterruptSystem.Initialize();

    static CLogger Logger(LogDebug, 0);
    Logger.Initialize(0);

    static CTimer Timer(&InterruptSystem);
    Timer.Initialize();

    static CUSBHCIDevice USBHCI(&InterruptSystem, &Timer, TRUE /* plug and play */);

    tfp_printf("circle: env + DWC2 USB host controller constructed\n");

    (void)DeviceNameService;
    (void)USBHCI;
}
