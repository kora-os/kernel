// SPDX-License-Identifier: GPL-3.0-or-later
//
// Key events to console bytes. See input/input_tty.h.

#include "input/input_tty.h"

#include "input/hid_keys.h"

static void sequence(struct input_tty_bytes *out, const char *bytes) {
    while (*bytes && out->length < INPUT_TTY_MAX_BYTES) {
        out->bytes[out->length++] = *bytes++;
    }
}

static void byte(struct input_tty_bytes *out, char c) {
    out->bytes[0] = c;
    out->length = 1;
}

// The control code Ctrl makes of an ASCII character, as on a VT terminal;
// characters without one pass unchanged.
static char control(char c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return (char)(c & 0x1F);
    }
    switch (c) {
    case ' ': case '2': case '@': return 0;
    case '[': case '{': return 0x1B;
    case '\\': case '|': return 0x1C;
    case ']': case '}': return 0x1D;
    case '6': case '^': return 0x1E;
    case '-': case '_': case '/': return 0x1F;
    case '?': return 0x7F;
    default: return c;
    }
}

void input_tty_translate(const struct input_event *event, struct input_tty_bytes *out) {
    *out = (struct input_tty_bytes){0};
    if (event->type != INPUT_EVENT_KEY_DOWN) {
        return;
    }
    bool shift = (event->modifiers & INPUT_MOD_SHIFT) != 0;
    switch (event->keycode) {
    case HID_KEY_PAGE_UP:
        if (shift) {
            out->scroll = 1;
        } else {
            sequence(out, "\x1B[5~");
        }
        return;
    case HID_KEY_PAGE_DOWN:
        if (shift) {
            out->scroll = -1;
        } else {
            sequence(out, "\x1B[6~");
        }
        return;
    case HID_KEY_UP: sequence(out, "\x1B[A"); return;
    case HID_KEY_DOWN: sequence(out, "\x1B[B"); return;
    case HID_KEY_RIGHT: sequence(out, "\x1B[C"); return;
    case HID_KEY_LEFT: sequence(out, "\x1B[D"); return;
    case HID_KEY_HOME: sequence(out, "\x1B[H"); return;
    case HID_KEY_END: sequence(out, "\x1B[F"); return;
    case HID_KEY_INSERT: sequence(out, "\x1B[2~"); return;
    case HID_KEY_DELETE: sequence(out, "\x1B[3~"); return;
    case HID_KEY_ENTER:
    case HID_KEY_KP_ENTER: byte(out, '\n'); return;
    case HID_KEY_TAB: byte(out, '\t'); return;
    case HID_KEY_BACKSPACE: byte(out, 0x7F); return;
    case HID_KEY_ESCAPE: byte(out, 0x1B); return;
    default: break;
    }
    uint32_t codepoint = event->codepoint;
    if (codepoint == 0 || codepoint > 0xFF) {
        return;
    }
    if (!(event->modifiers & INPUT_MOD_CTRL)) {
        byte(out, (char)codepoint);
    } else if (codepoint < 0x80) {
        byte(out, control((char)codepoint));
    }
}
