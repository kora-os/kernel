// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_input.h"

#include "input/hid_keys.h"

#define EVDEV_CODES 128

// Linux evdev key codes to USB HID usages (the key's position); 0 for codes
// that are not on a keyboard.
static const uint8_t usages[EVDEV_CODES] = {
    [1] = 0x29, [2] = 0x1E, [3] = 0x1F, [4] = 0x20, [5] = 0x21, [6] = 0x22,
    [7] = 0x23, [8] = 0x24, [9] = 0x25, [10] = 0x26, [11] = 0x27, [12] = 0x2D,
    [13] = 0x2E, [14] = 0x2A, [15] = 0x2B, [16] = 0x14, [17] = 0x1A, [18] = 0x08,
    [19] = 0x15, [20] = 0x17, [21] = 0x1C, [22] = 0x18, [23] = 0x0C, [24] = 0x12,
    [25] = 0x13, [26] = 0x2F, [27] = 0x30, [28] = 0x28, [29] = 0xE0, [30] = 0x04,
    [31] = 0x16, [32] = 0x07, [33] = 0x09, [34] = 0x0A, [35] = 0x0B, [36] = 0x0D,
    [37] = 0x0E, [38] = 0x0F, [39] = 0x33, [40] = 0x34, [41] = 0x35, [42] = 0xE1,
    [43] = 0x31, [44] = 0x1D, [45] = 0x1B, [46] = 0x06, [47] = 0x19, [48] = 0x05,
    [49] = 0x11, [50] = 0x10, [51] = 0x36, [52] = 0x37, [53] = 0x38, [54] = 0xE5,
    [55] = 0x55, [56] = 0xE2, [57] = 0x2C, [58] = 0x39, [59] = 0x3A, [60] = 0x3B,
    [61] = 0x3C, [62] = 0x3D, [63] = 0x3E, [64] = 0x3F, [65] = 0x40, [66] = 0x41,
    [67] = 0x42, [68] = 0x43, [69] = 0x53, [70] = 0x47, [71] = 0x5F, [72] = 0x60,
    [73] = 0x61, [74] = 0x56, [75] = 0x5C, [76] = 0x5D, [77] = 0x5E, [78] = 0x57,
    [79] = 0x59, [80] = 0x5A, [81] = 0x5B, [82] = 0x62, [83] = 0x63, [86] = 0x64,
    [87] = 0x44, [88] = 0x45, [96] = 0x58, [97] = 0xE4, [98] = 0x54, [99] = 0x46,
    [100] = 0xE6, [102] = 0x4A, [103] = 0x52, [104] = 0x4B, [105] = 0x50,
    [106] = 0x4F, [107] = 0x4D, [108] = 0x51, [109] = 0x4E, [110] = 0x49,
    [111] = 0x4C, [119] = 0x48, [125] = 0xE3, [126] = 0xE7, [127] = 0x65
};

// The characters of the US layout, by evdev code. Keys that type none (Enter,
// Tab, Backspace, Escape, the navigation keys) are known by their usage.
static const char unshifted[EVDEV_CODES] = {
    [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5',
    [7] = '6', [8] = '7', [9] = '8', [10] = '9', [11] = '0', [12] = '-',
    [13] = '=', [16] = 'q', [17] = 'w', [18] = 'e',
    [19] = 'r', [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o',
    [25] = 'p', [26] = '[', [27] = ']', [30] = 'a', [31] = 's',
    [32] = 'd', [33] = 'f', [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k',
    [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',
    [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n', [50] = 'm',
    [51] = ',', [52] = '.', [53] = '/', [57] = ' ', [98] = '/'
};
static const char shifted[EVDEV_CODES] = {
    [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%', [7] = '^',
    [8] = '&', [9] = '*', [10] = '(', [11] = ')', [12] = '_', [13] = '+',
    [26] = '{', [27] = '}', [39] = ':', [40] = '"', [41] = '~', [43] = '|',
    [51] = '<', [52] = '>', [53] = '?'
};

// The character the key types under Shift and Caps Lock, or 0.
static char character(const struct virtio_keymap_state *state, uint16_t code) {
    bool shift = (state->modifiers & INPUT_MOD_SHIFT) != 0;
    char c = unshifted[code];
    if (c >= 'a' && c <= 'z') {
        if (shift != state->caps_lock) {
            c -= 'a' - 'A';
        }
    } else if (shift && shifted[code]) {
        c = shifted[code];
    }
    return c;
}

bool virtio_keymap_event(struct virtio_keymap_state *state, uint16_t type,
                         uint16_t code, uint32_t value, struct input_event *event) {
    if (!state || !event) {
        return false;
    }
    if (type == 0 && code == 3) { // EV_SYN/SYN_DROPPED: avoid stuck modifiers
        *state = (struct virtio_keymap_state){0};
        return false;
    }
    if (type != 1 || value > 2 || code >= EVDEV_CODES) { // EV_KEY only; release, press, repeat
        return false;
    }
    uint8_t usage = usages[code];
    if (!usage) {
        return false;
    }
    if (usage >= HID_KEY_LEFT_CTRL && usage <= HID_KEY_RIGHT_GUI) {
        uint8_t mask = (uint8_t)(1u << (usage - HID_KEY_LEFT_CTRL));
        state->modifiers = value ? state->modifiers | mask : state->modifiers & ~mask;
    } else if (usage == HID_KEY_CAPS_LOCK) {
        if (value == 1 && !state->caps_down) {
            state->caps_lock = !state->caps_lock;
        }
        state->caps_down = value != 0;
    }
    *event = (struct input_event){
        .type = value ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP,
        .flags = value == 2 ? INPUT_FLAG_REPEAT : 0,
        .modifiers = state->modifiers | (state->caps_lock ? INPUT_MOD_CAPS_LOCK : 0),
        .keycode = usage,
        .codepoint = value ? (uint8_t)character(state, code) : 0
    };
    return true;
}
