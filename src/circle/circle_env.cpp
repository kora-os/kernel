// SPDX-License-Identifier: GPL-3.0-or-later
//
// Construction and bring-up of the vendored Circle USB stack on the KoraOS HAL
// bridge: the host controller for this board (CUSBHCIDevice is the DWC2 on the
// Pi 3, the xHCI behind PCIe on the Pi 4), device enumeration, the USB
// keyboard feeding the input event queue and the mass storage drives for the
// block layer.
// QEMU's raspi4b has no PCIe, so the caller gates enumeration with `enumerate`.

#include "circle_env.h"

#include <circle/devicenameservice.h>
#include <circle/input/keymap.h>
#include <circle/interrupt.h>
#include <circle/logger.h>
#include <circle/machineinfo.h>
#include <circle/spinlock.h>
#include <circle/timer.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/usb/usbkeyboard.h>
#include <circle/input/mouse.h>

#include "drivers/usb_storage.h"
#include "input/hid_keyboard.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "mm/mmu.h"

extern "C" void tfp_printf(const char *fmt, ...);

namespace {

// The USB keyboard, in Circle's raw mode: each HID boot report (IRQ context,
// core 0) is compared with the one before, and every key that went down or up
// is posted to the input event queue. Circle's cooked mode, which hands over
// strings, is not used.
struct hid_keyboard usb_keyboard;

// Circle's keymap gives the character of a key under the layout picked at
// build time (KORAOS_KEYMAP); it also keeps the Caps Lock and Num Lock state.
CKeyMap *usb_keymap;
static u8 usb_keyboard_source;  // input_source_register()'s number

// The character a key types, as a Unicode code point, or 0 for none. Call
// once for each key that goes down: Translate() is what toggles the locks.
// `level` holds the modifiers that pick a column of the layout (Shift and
// AltGr); Ctrl, Alt and GUI are left out, since they do not change the
// character.
uint32_t key_character(uint8_t usage, uint8_t level) {
    u16 code = usb_keymap->Translate(usage, level);
    char buffer[2];
    const char *string = usb_keymap->GetString(code, 0, buffer);
    // Keys that type nothing have no string, a control code (Enter, Tab,
    // Backspace, Escape) or an escape sequence (arrows, function keys).
    if (string == 0 || string[0] == '\0' || string[1] != '\0') {
        return 0;
    }
    unsigned char c = (unsigned char)string[0];
    if (c < 0x20 || c == 0x7F) {
        return 0;
    }
    // The keymaps are in Windows-1252, which is Unicode outside 0x80 to 0x9F;
    // of that range they only use the euro sign.
    if (c == 0x80) {
        return 0x20AC;
    }
    return c >= 0x80 && c <= 0x9F ? 0 : c;
}

void key_changed(void *, uint8_t usage, bool down, uint8_t modifiers) {
    struct input_event event = {};
    event.type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP;
    event.keycode = usage;
    event.source = usb_keyboard_source;
    if (down && usage < HID_KEY_LEFT_CTRL) {
        event.codepoint = key_character(
            usage, modifiers & (KEY_LSHIFT_MASK | KEY_RSHIFT_MASK | KEY_ALTGR_MASK));
    }
    u8 locks = usb_keymap->GetLEDStatus();
    event.modifiers = modifiers;  // a report's modifier byte is INPUT_MOD_*'s low byte
    if (locks & KEYB_LED_CAPS_LOCK) {
        event.modifiers |= INPUT_MOD_CAPS_LOCK;
    }
    if (locks & KEYB_LED_NUM_LOCK) {
        event.modifiers |= INPUT_MOD_NUM_LOCK;
    }
    input_post(&event);
}

void key_status_raw(unsigned char ucModifiers, const unsigned char RawKeys[6]) {
    hid_keyboard_report(&usb_keyboard, ucModifiers, RawKeys, key_changed, 0);
}

// The USB mouse, in Circle's raw mode: each report (IRQ context, core 0) is a
// movement, the buttons held (Circle's MOUSE_BUTTON_* are INPUT_BUTTON_*) and
// a turn of the wheel (away from the user > 0, as in HID).
static u8 usb_mouse_source;

int16_t clamp16(int v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

void mouse_status(unsigned nButtons, int nDisplacementX, int nDisplacementY, int nWheelMove,
                  void *) {
    struct input_event event = {};
    event.type = INPUT_EVENT_MOUSE;
    event.source = usb_mouse_source;
    event.x = clamp16(nDisplacementX);
    event.y = clamp16(nDisplacementY);
    event.buttons = (u8)(nButtons & 0x1F);
    event.wheel = (int8_t)(nWheelMove < -128 ? -128 : nWheelMove > 127 ? 127 : nWheelMove);
    input_post(&event);
}

}  // namespace

void circle_usb_init(int enumerate) {
    // Circle is built multi-core (filesystem calls reach USB storage from any
    // core): its spinlocks only lock once enabled.
    CSpinLock::Enable();

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

    // Board model, RAM size and (Pi 4) the firmware's device tree, which the
    // xHCI driver needs for the PCIe DMA window. CMachineInfo clears the
    // firmware's device-tree pointer in page 0 after reading it.
    mmu_set_firmware_page_writable(1);
    static CMachineInfo MachineInfo;
    mmu_set_firmware_page_writable(0);
    tfp_printf("circle: %s rev %u, %u MB RAM\n", MachineInfo.GetMachineName(),
               MachineInfo.GetModelRevision(), MachineInfo.GetRAMSize());
#if RASPPI >= 4
    TMemoryWindow dma = MachineInfo.GetPCIeDMAMemory(PCIE_BUS_XHCI);
    tfp_printf("circle: device tree %s; PCIe DMA: bus 0x%lx -> cpu 0x%lx, size 0x%lx\n",
               MachineInfo.GetDTB() != 0 ? "found" : "NOT found (using defaults)",
               (unsigned long)dma.BusAddress, (unsigned long)dma.CPUAddress,
               (unsigned long)dma.Size);
    TMemoryWindow mmio = MachineInfo.GetPCIeMemory(PCIE_BUS_XHCI);
    tfp_printf("circle: PCIe MMIO: cpu 0x%lx -> bus 0x%lx, size 0x%lx\n",
               (unsigned long)mmio.CPUAddress, (unsigned long)mmio.BusAddress,
               (unsigned long)mmio.Size);
#endif

    static CUSBHCIDevice USBHCI(&InterruptSystem, &Timer, FALSE /* no plug&play */);

    if (!enumerate) {
        tfp_printf("circle: USB host controller (%s) constructed, enumeration skipped\n",
                   RASPPI >= 4 ? "xHCI" : "DWC2");
        return;
    }

#if RASPPI >= 4
    // PCIe link and reset, the VideoCore loading the VL805's firmware, the xHCI
    // reset, then enumeration of the root ports (Circle logs each failure).
    tfp_printf("circle: initializing USB host controller (xHCI via PCIe)...\n");
#else
    tfp_printf("circle: initializing USB host controller (DWC2)...\n");
#endif
    if (!USBHCI.Initialize()) {
        tfp_printf("circle: USB host controller init FAILED\n");
        return;
    }
    usb_storage_collect();

    CMouseDevice *pMouse = (CMouseDevice *)DeviceNameService.GetDevice("mouse", 1, FALSE);
    if (pMouse != 0) {
        usb_mouse_source = (u8)input_source_register("usb-mouse");
        pMouse->RegisterStatusHandler(mouse_status, 0);
        tfp_printf("circle: USB mouse ready\n");
    }

    CUSBKeyboardDevice *pKeyboard =
        (CUSBKeyboardDevice *)DeviceNameService.GetDevice("ukbd", 1, FALSE);
    if (pKeyboard == 0) {
        tfp_printf("circle: no USB keyboard found (is one plugged in?)\n");
        return;
    }

    static CKeyMap KeyMap;
    usb_keymap = &KeyMap;
    usb_keyboard_source = (u8)input_source_register("usb-keyboard");
    pKeyboard->RegisterKeyStatusHandlerRaw(key_status_raw, FALSE /* raw only */);
    tfp_printf("circle: USB keyboard ready -- input goes to the screen terminal\n");
}
