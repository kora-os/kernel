// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Input events: what a keyboard or a mouse did, as data. The kernel carries
// them from the drivers to one consumer through the queue in input/input.h and
// gives them no meaning: turning keys into bytes or escape sequences, and
// movements into a pointer on the screen, is the consumer's business (the
// screen manager, user/screens.c; the tty adapter in input/input_tty.h while
// none runs).

#define INPUT_EVENT_KEY_DOWN 1
#define INPUT_EVENT_KEY_UP   2
// A mouse's report: a movement, the buttons held after it and a turn of the
// wheel. The consumer keeps the pointer's position (console-plan.md, step 5).
#define INPUT_EVENT_MOUSE    3

// Key down only: the key is held and this is an auto-repeat, not a new press.
#define INPUT_FLAG_REPEAT 0x01
// Mouse only: x and y are a position, each 0 to INPUT_ABS_MAX across the
// display (a tablet, or QEMU's), not a movement.
#define INPUT_FLAG_ABSOLUTE 0x02
#define INPUT_ABS_MAX 32767

// Mouse buttons held, as in a USB HID boot mouse report.
#define INPUT_BUTTON_LEFT   0x01
#define INPUT_BUTTON_RIGHT  0x02
#define INPUT_BUTTON_MIDDLE 0x04
#define INPUT_BUTTON_SIDE   0x08
#define INPUT_BUTTON_EXTRA  0x10

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
    union {
        // The key as a position on the keyboard, whatever the layout prints
        // on it: a usage of the USB HID Keyboard/Keypad page (input/hid_keys.h).
        uint16_t keycode;
        // Mouse: the movement to the right in its own units (or the position
        // across, with INPUT_FLAG_ABSOLUTE).
        int16_t x;
    };
    // The device that sent it: a number input_source_register() handed its
    // driver (input/input.h), or 0 for none.
    uint8_t source;
    union {
        uint8_t reserved;  // keys
        uint8_t buttons;   // mouse: INPUT_BUTTON_* held after this report
    };
    union {
        // Key down: the Unicode character the key types under the layout,
        // Shift, Caps Lock and AltGr, or 0 for a key that types none (Enter,
        // arrows, modifiers). Ctrl, Alt and GUI do not change it. Key up: 0.
        uint32_t codepoint;
        struct {
            int16_t y;      // mouse: the movement down (or the position down)
            int8_t wheel;   // mouse: the wheel's turn, away from the user > 0
            uint8_t unused;
        };
    };
};
#ifdef __cplusplus
static_assert(sizeof(struct input_event) == 16, "input event layout");
#else
_Static_assert(sizeof(struct input_event) == 16, "input event layout");
_Static_assert((int8_t)-1 < 0, "int8_t is signed: a wheel turns both ways");
_Static_assert(__builtin_offsetof(struct input_event, source) == 10 &&
                   __builtin_offsetof(struct input_event, buttons) == 11 &&
                   __builtin_offsetof(struct input_event, y) == 12 &&
                   __builtin_offsetof(struct input_event, wheel) == 14,
               "mouse fields of the input event");
#endif
