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

#include "tty.h"

extern "C" void tfp_printf(const char *fmt, ...);

namespace {

// Cooked keystrokes from the USB keyboard (IRQ context) feed the tty input
// queue that read(fd 0) waits on. Multi-byte strings are escape sequences for
// arrows, function keys and the like, which nothing consumes yet: drop them.
void key_pressed_handler(const char *pString) {
    if (pString[0] != '\0' && pString[1] == '\0') {
        tty_input_push(pString[0]);
    }
}

// Shift+PageUp / Shift+PageDown scroll the screen's history. Circle's keymaps
// give these no cooked string, so they are picked out of the raw HID boot
// reports; the handler runs in mixed mode, so cooked keys still flow above.
constexpr unsigned char kHidPageUp = 0x4B;
constexpr unsigned char kHidPageDown = 0x4E;
constexpr unsigned char kHidShiftMask = 0x02 | 0x20;  // left / right shift

void key_status_raw(unsigned char ucModifiers, const unsigned char RawKeys[6]) {
    static unsigned char previous[6];
    for (unsigned i = 0; i < 6; i++) {
        unsigned char key = RawKeys[i];
        bool held_before = false;
        for (unsigned j = 0; j < 6; j++) {
            held_before |= key == previous[j];
        }
        if (key == 0 || held_before || !(ucModifiers & kHidShiftMask)) {
            continue;
        }
        if (key == kHidPageUp) {
            tty_scrollback(1);
        } else if (key == kHidPageDown) {
            tty_scrollback(-1);
        }
    }
    for (unsigned i = 0; i < 6; i++) {
        previous[i] = RawKeys[i];
    }
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
    pKeyboard->RegisterKeyStatusHandlerRaw(key_status_raw, TRUE /* mixed mode */);
    tfp_printf("circle: USB keyboard ready -- input goes to the screen terminal\n");
}
