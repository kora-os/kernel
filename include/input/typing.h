// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// Keys from text, for the debug console's `type` and `key` (console.c): what
// a US keyboard would do to type a character or press a named key, as the
// key code (a USB HID usage), the modifiers held (INPUT_MOD_*) and the
// character it types (0 for none), as the keyboard drivers report them.
// Pure, so the host tests check it.

struct typed_key {
    uint16_t keycode;
    uint16_t modifiers;
    uint32_t codepoint;
};

// The key that types byte `c`: printable ASCII, '\n' (Enter), '\t' (Tab),
// 0x1B (Escape), 0x7F (Backspace), and the control codes 0x00 to 0x1F as
// Ctrl with a letter or a sign (Ctrl+C for 0x03). False for other bytes.
bool typing_key_for_char(unsigned char c, struct typed_key *out);

// The key named `name` (`length` bytes), as QEMU's sendkey names keys:
// modifiers first, joined by '-' (ctrl, shift, alt, meta_l, meta_r; "meta" is
// meta_l), then one key: a letter or digit, spc, ret, backspace, esc, tab,
// up, down, left, right, home, end, delete, insert, pgup, pgdn, f1 to f12.
// So "ctrl-c", "shift-pgup", "meta_l-m". False for an unknown name.
bool typing_key_for_name(const char *name, size_t length, struct typed_key *out);

// The bytes of `text` with escapes resolved: \n, \t, \e (Escape), \\, \s (a
// space, for a leading one) and \xHH. Returns how many were written to `out`
// (at most `size`), or -1 for a broken escape.
int typing_unescape(const char *text, char *out, size_t size);

// A mouse from text, for the debug console's `mouse`: what a mouse would
// report for one of
//   DX DY        a movement (each -32768 to 32767)
//   to X Y       a position, as a tablet reports it (each 0 to INPUT_ABS_MAX)
//   down B, up B, click B   a button pressed, released, or both (B: left,
//                right, middle, side or extra)
//   wheel N      a turn of the wheel (-128 to 127, away from the user > 0)
// `*buttons` holds the buttons held, before and after. Fills `events` (room
// for 2) as INPUT_EVENT_MOUSE (but for their time and source) and returns
// how many, or 0 for a command it does not know (`*buttons` untouched).
unsigned typing_mouse(const char *args, uint8_t *buttons, struct input_event events[2]);
