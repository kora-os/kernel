// SPDX-License-Identifier: GPL-3.0-or-later
//
// Construction and bring-up of the vendored Circle USB stack on the KoraOS HAL
// bridge. Step 3c initializes the DWC2 host controller, enumerates devices, and
// logs keystrokes from an attached USB keyboard. Enumeration needs real hardware
// (QEMU raspi3b has no USB), so the caller gates it with `enumerate`.

#include "circle_env.h"

#include <circle/devicenameservice.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>

extern "C" void tfp_printf(const char *fmt, ...);

namespace {

// Cooked keystrokes from the USB keyboard. Step 3c just echoes them to the
// console over UART; wiring them into read(fd 0) is the final milestone step.
void key_pressed_handler(const char *pString) {
    tfp_printf("%s", pString);
}

}  // namespace

void circle_usb_init(int enumerate) {
    // Constructed once, with static storage duration. Order matters: name
    // service and interrupt system first, then logger and timer, then the USB
    // host controller that depends on them. bPlugAndPlay = FALSE so Initialize()
    // enumerates whatever is attached at boot (a keyboard must be plugged in).
    static CDeviceNameService DeviceNameService;
    static CInterruptSystem InterruptSystem;
    InterruptSystem.Initialize();

    static CLogger Logger(LogDebug, 0);
    Logger.Initialize(0);

    static CTimer Timer(&InterruptSystem);
    Timer.Initialize();

    static CUSBHCIDevice USBHCI(&InterruptSystem, &Timer, FALSE /* no plug&play */);

    if (!enumerate) {
        tfp_printf("circle: env + DWC2 USB host controller constructed "
                   "(enumeration skipped)\n");
        return;
    }

    tfp_printf("circle: initializing USB host controller...\n");
    if (!USBHCI.Initialize()) {
        tfp_printf("circle: USB host controller init FAILED\n");
        return;
    }

    CUSBKeyboardDevice *pKeyboard =
        (CUSBKeyboardDevice *)DeviceNameService.GetDevice("ukbd", 1, FALSE);
    if (pKeyboard == 0) {
        tfp_printf("circle: no USB keyboard found (is one plugged in?)\n");
        return;
    }

    pKeyboard->RegisterKeyPressedHandler(key_pressed_handler);
    tfp_printf("circle: USB keyboard ready -- type to see keys over UART\n");
}
