// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// The temporary adapter of console-plan.md, step 1: it turns key events back
// into the bytes and escape sequences programs read on the console today, so
// nothing above the tty changes while the console moves to user space. It
// goes away when the screen manager reads the events itself (step 4).
//
// Pure and stateless: everything it needs is in the event.

#define INPUT_TTY_MAX_BYTES 4  // the longest sequence one key produces

struct input_tty_bytes {
    char bytes[INPUT_TTY_MAX_BYTES];  // may hold a NUL (Ctrl+Space)
    unsigned length;
    int scroll;  // half screens of history to scroll back by (negative: forward)
};

// What a terminal sends for `event`:
//   - key up: nothing;
//   - Shift+PageUp / Shift+PageDown: a scrollback request, no bytes;
//   - arrows, Home, End, Insert, Delete, PageUp, PageDown: their xterm
//     sequences (ESC [ A and so on);
//   - Enter '\n', Tab, Backspace 0x7F, Escape;
//   - a key with a character: that character as one byte (code points above
//     0xFF have no byte and are dropped), folded to a control code with Ctrl.
// Alt and GUI change nothing. A repeat counts as a press.
void input_tty_translate(const struct input_event *event, struct input_tty_bytes *out);
