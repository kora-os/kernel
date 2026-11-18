// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Key codes of input events: usages of the USB HID Keyboard/Keypad page
// (0x07), as in the HID Usage Tables. Only the keys the kernel names are
// listed; every other usage is carried through unchanged. Letters run from
// HID_KEY_A in alphabetical order, whatever the layout.

#define HID_KEY_ROLLOVER    0x01  // in a report: too many keys held (no key)
#define HID_KEY_FIRST       0x04  // lower usages are not keys
#define HID_KEY_A           0x04
#define HID_KEY_Z           0x1D
#define HID_KEY_ENTER       0x28
#define HID_KEY_ESCAPE      0x29
#define HID_KEY_BACKSPACE   0x2A
#define HID_KEY_TAB         0x2B
#define HID_KEY_SPACE       0x2C
#define HID_KEY_CAPS_LOCK   0x39
#define HID_KEY_F1          0x3A
#define HID_KEY_F12         0x45
#define HID_KEY_INSERT      0x49
#define HID_KEY_HOME        0x4A
#define HID_KEY_PAGE_UP     0x4B
#define HID_KEY_DELETE      0x4C
#define HID_KEY_END         0x4D
#define HID_KEY_PAGE_DOWN   0x4E
#define HID_KEY_RIGHT       0x4F
#define HID_KEY_LEFT        0x50
#define HID_KEY_DOWN        0x51
#define HID_KEY_UP          0x52
#define HID_KEY_NUM_LOCK    0x53
#define HID_KEY_KP_ENTER    0x58

// The eight modifier keys, in the bit order of a report's modifier byte and
// of INPUT_MOD_* (input/event.h): Ctrl, Shift, Alt, GUI, left then right.
#define HID_KEY_LEFT_CTRL   0xE0
#define HID_KEY_RIGHT_GUI   0xE7
