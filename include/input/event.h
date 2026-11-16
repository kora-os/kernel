// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Input events: what a keyboard did, as data. The kernel carries them from the
// drivers to one consumer through the queue in input/input.h and gives them no
// meaning as text: turning them into bytes or escape sequences is the
// consumer's business (the tty adapter in input/input_tty.h for now, the
// console of project_doc/console-plan.md later).

#define INPUT_EVENT_KEY_DOWN 1
#define INPUT_EVENT_KEY_UP   2
// Mouse events take the next type numbers (console-plan.md, step 5).

// Key down only: the key is held and this is an auto-repeat, not a new press.
#define INPUT_FLAG_REPEAT 0x01

// Modifier state. The low byte is the modifier byte of a USB HID keyboard
// report, bit n being the key with usage 0xE0 + n; the locks follow.
#define INPUT_MOD_LEFT_CTRL   0x0001
#define INPUT_MOD_LEFT_SHIFT  0x0002
#define INPUT_MOD_LEFT_ALT    0x0004
#define INPUT_MOD_LEFT_GUI    0x0008
#define INPUT_MOD_RIGHT_CTRL  0x0010
#define INPUT_MOD_RIGHT_SHIFT 0x0020
#define INPUT_MOD_RIGHT_ALT   0x0040  // AltGr where the layout has one
#define INPUT_MOD_RIGHT_GUI   0x0080
#define INPUT_MOD_CAPS_LOCK   0x0100
#define INPUT_MOD_NUM_LOCK    0x0200

#define INPUT_MOD_CTRL  (INPUT_MOD_LEFT_CTRL | INPUT_MOD_RIGHT_CTRL)
#define INPUT_MOD_SHIFT (INPUT_MOD_LEFT_SHIFT | INPUT_MOD_RIGHT_SHIFT)

struct input_event {
    uint8_t type;        // INPUT_EVENT_*
    uint8_t flags;       // INPUT_FLAG_*
    // INPUT_MOD_* after this event: a modifier key's own down event has its
    // bit set, its up event has it clear.
    uint16_t modifiers;
    uint32_t time_ms;    // uptime when queued; set by input_post()
    // The key as a position on the keyboard, whatever the layout prints on
    // it: a usage of the USB HID Keyboard/Keypad page (input/hid_keys.h).
    uint16_t keycode;
    uint16_t reserved;
    // Key down: the Unicode character the key types under the layout, Shift,
    // Caps Lock and AltGr, or 0 for a key that types none (Enter, arrows,
    // modifiers). Ctrl, Alt and GUI do not change it. Key up: 0.
    uint32_t codepoint;
};
#ifdef __cplusplus
static_assert(sizeof(struct input_event) == 16, "input event layout");
#else
_Static_assert(sizeof(struct input_event) == 16, "input event layout");
#endif
