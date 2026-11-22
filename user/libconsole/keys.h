// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libconsole/con_base.h"

// Key events to the bytes a terminal sends for them, as xterm does: what a
// console hands its programs in raw mode, and what its line editor reads in
// cooked mode (libconsole/edit.h). Pure: built for the kernel and for user
// programs, it knows the event's fields but not its struct.

// The key positions (USB HID usages) and the modifier bits this needs, as
// input events carry them (include/input/event.h, <libk/koraos.h>).
#define CON_KEY_ENTER 0x28
#define CON_KEY_ESCAPE 0x29
#define CON_KEY_BACKSPACE 0x2A
#define CON_KEY_TAB 0x2B
#define CON_KEY_INSERT 0x49
#define CON_KEY_HOME 0x4A
#define CON_KEY_PAGE_UP 0x4B
#define CON_KEY_DELETE 0x4C
#define CON_KEY_END 0x4D
#define CON_KEY_PAGE_DOWN 0x4E
#define CON_KEY_RIGHT 0x4F
#define CON_KEY_LEFT 0x50
#define CON_KEY_DOWN 0x51
#define CON_KEY_UP 0x52
#define CON_KEY_KP_ENTER 0x58
#define CON_MOD_CTRL 0x0011
#define CON_MOD_SHIFT 0x0022

#define CON_KEY_DOWN_EVENT 1  // the event type of a key press
#define CON_KEY_BYTES_MAX 6   // the longest sequence one key produces

typedef struct {
    char bytes[CON_KEY_BYTES_MAX];  // may hold a NUL (Ctrl+Space)
    unsigned length;
    // Shift+PageUp and Shift+PageDown scroll the console's history instead:
    // half screens back (positive) or forward (negative).
    int scroll;
} con_key_t;

// What a key event of `type` sends: nothing for a key up, a modifier alone or
// a key that types nothing. Arrows, Home, End, Insert, Delete and PageUp and
// PageDown are xterm sequences, Enter is '\n', Backspace DEL (0x7F), Ctrl
// with a letter or some punctuation a control code; the character a key
// types under the layout comes as UTF-8. Alt and Meta are ignored.
void con_key_bytes(unsigned type, unsigned modifiers, unsigned keycode, uint32_t codepoint,
                   con_key_t *out);
