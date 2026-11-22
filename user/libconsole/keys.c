// SPDX-License-Identifier: GPL-3.0-or-later
//
// Key events to terminal bytes. See keys.h.

#include "libconsole/keys.h"

static void sequence(con_key_t *out, const char *bytes) {
    while (*bytes && out->length < CON_KEY_BYTES_MAX) {
        out->bytes[out->length++] = *bytes++;
    }
}

static void byte(con_key_t *out, char c) {
    out->bytes[0] = c;
    out->length = 1;
}

// The control code Ctrl makes of an ASCII character, as on a VT terminal, or
// -1 for one that has none.
static int control(uint32_t c) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
        return (int)(c & 0x1F);
    }
    switch (c) {
    case ' ': case '2': case '@': return 0;
    case '[': case '{': return 0x1B;
    case '\\': case '|': return 0x1C;
    case ']': case '}': return 0x1D;
    case '6': case '^': return 0x1E;
    case '-': case '_': case '/': return 0x1F;
    case '?': return 0x7F;
    default: return -1;
    }
}

static void utf8(con_key_t *out, uint32_t c) {
    if (c < 0x80) {
        byte(out, (char)c);
    } else if (c < 0x800) {
        out->bytes[0] = (char)(0xC0 | (c >> 6));
        out->bytes[1] = (char)(0x80 | (c & 0x3F));
        out->length = 2;
    } else if (c < 0x10000) {
        if (c >= 0xD800 && c < 0xE000) {
            return;  // a surrogate is no character
        }
        out->bytes[0] = (char)(0xE0 | (c >> 12));
        out->bytes[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        out->bytes[2] = (char)(0x80 | (c & 0x3F));
        out->length = 3;
    } else if (c < 0x110000) {
        out->bytes[0] = (char)(0xF0 | (c >> 18));
        out->bytes[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        out->bytes[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        out->bytes[3] = (char)(0x80 | (c & 0x3F));
        out->length = 4;
    }
}

void con_key_bytes(unsigned type, unsigned modifiers, unsigned keycode, uint32_t codepoint,
                   con_key_t *out) {
    *out = (con_key_t){0};
    if (type != CON_KEY_DOWN_EVENT) {
        return;
    }
    bool shift = (modifiers & CON_MOD_SHIFT) != 0;
    switch (keycode) {
    case CON_KEY_PAGE_UP:
        if (shift) {
            out->scroll = 1;
        } else {
            sequence(out, "\x1B[5~");
        }
        return;
    case CON_KEY_PAGE_DOWN:
        if (shift) {
            out->scroll = -1;
        } else {
            sequence(out, "\x1B[6~");
        }
        return;
    case CON_KEY_UP: sequence(out, "\x1B[A"); return;
    case CON_KEY_DOWN: sequence(out, "\x1B[B"); return;
    case CON_KEY_RIGHT: sequence(out, "\x1B[C"); return;
    case CON_KEY_LEFT: sequence(out, "\x1B[D"); return;
    case CON_KEY_HOME: sequence(out, "\x1B[H"); return;
    case CON_KEY_END: sequence(out, "\x1B[F"); return;
    case CON_KEY_INSERT: sequence(out, "\x1B[2~"); return;
    case CON_KEY_DELETE: sequence(out, "\x1B[3~"); return;
    case CON_KEY_ENTER:
    case CON_KEY_KP_ENTER: byte(out, '\n'); return;
    case CON_KEY_TAB: byte(out, '\t'); return;
    case CON_KEY_BACKSPACE: byte(out, 0x7F); return;
    case CON_KEY_ESCAPE: byte(out, 0x1B); return;
    default: break;
    }
    if (codepoint == 0) {
        return;
    }
    if (modifiers & CON_MOD_CTRL) {
        int code = control(codepoint);
        if (code >= 0) {
            byte(out, (char)code);
        }
        return;
    }
    utf8(out, codepoint);
}
