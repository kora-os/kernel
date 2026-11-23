// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keys from text for the debug console. See input/typing.h.

#include "input/typing.h"

#include "input/event.h"
#include "input/hid_keys.h"
#include "lib/string.h"

// The US layout: for each usage from 0x1E (the digit row) to 0x38, the
// character it types, and with Shift.
static const char plain[] = "1234567890\n\x1b\x7f\t -=[]\\#;'`,./";
static const char shifted[] = "!@#$%^&*()\n\x1b\x7f\t _+{}|~:\"~<>?";
#define FIRST_PLAIN 0x1E

static bool from_layout(unsigned char c, struct typed_key *out) {
    for (unsigned i = 0; plain[i]; i++) {
        uint16_t usage = (uint16_t)(FIRST_PLAIN + i);
        if (usage == 0x32) {
            continue;  // Non-US #: not on a US keyboard
        }
        if ((unsigned char)plain[i] == c || (unsigned char)shifted[i] == c) {
            bool shift = (unsigned char)plain[i] != c;
            bool types = c >= 0x20 && c < 0x7F;
            *out = (struct typed_key){usage, shift ? INPUT_MOD_LEFT_SHIFT : 0, types ? c : 0};
            return true;
        }
    }
    return false;
}

bool typing_key_for_char(unsigned char c, struct typed_key *out) {
    if (c >= 'a' && c <= 'z') {
        *out = (struct typed_key){(uint16_t)(HID_KEY_A + (c - 'a')), 0, c};
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        *out = (struct typed_key){(uint16_t)(HID_KEY_A + (c - 'A')), INPUT_MOD_LEFT_SHIFT, c};
        return true;
    }
    if (from_layout(c, out)) {
        return true;
    }
    // Control codes: Ctrl with the key of the character 0x40 above (Ctrl+C
    // is 0x03), which the key types without Ctrl.
    if (c < 0x20) {
        unsigned char base = (unsigned char)(c + 0x60);  // 0x01 -> 'a'
        if (c == 0) {
            base = ' ';
        } else if (c >= 0x1B) {
            base = (unsigned char)"[\\]^_"[c - 0x1B];
        }
        struct typed_key key;
        if (!typing_key_for_char(base, &key)) {
            return false;
        }
        *out = (struct typed_key){key.keycode,
                                  (uint16_t)(key.modifiers | INPUT_MOD_LEFT_CTRL), key.codepoint};
        return true;
    }
    return false;
}

static const struct {
    const char *name;
    uint16_t usage;
} named[] = {
    {"spc", HID_KEY_SPACE}, {"ret", HID_KEY_ENTER}, {"backspace", HID_KEY_BACKSPACE},
    {"esc", HID_KEY_ESCAPE}, {"tab", HID_KEY_TAB}, {"up", HID_KEY_UP}, {"down", HID_KEY_DOWN},
    {"left", HID_KEY_LEFT}, {"right", HID_KEY_RIGHT}, {"home", HID_KEY_HOME},
    {"end", HID_KEY_END}, {"delete", HID_KEY_DELETE}, {"insert", HID_KEY_INSERT},
    {"pgup", HID_KEY_PAGE_UP}, {"pgdn", HID_KEY_PAGE_DOWN},
};

static const struct {
    const char *name;
    uint16_t modifier;
} modifiers[] = {
    {"ctrl", INPUT_MOD_LEFT_CTRL}, {"shift", INPUT_MOD_LEFT_SHIFT}, {"alt", INPUT_MOD_LEFT_ALT},
    {"meta_l", INPUT_MOD_LEFT_GUI}, {"meta", INPUT_MOD_LEFT_GUI}, {"meta_r", INPUT_MOD_RIGHT_GUI},
};

static bool equal(const char *a, size_t length, const char *b) {
    return (size_t)strlen(b) == length && strncmp(a, b, length) == 0;
}

bool typing_key_for_name(const char *name, size_t length, struct typed_key *out) {
    uint16_t held = 0;
    for (;;) {
        size_t part = 0;
        while (part < length && name[part] != '-') {
            part++;
        }
        if (part == length || part == 0) {
            break;  // the last part is the key ("-" alone too)
        }
        bool found = false;
        for (unsigned i = 0; i < sizeof(modifiers) / sizeof(modifiers[0]); i++) {
            if (equal(name, part, modifiers[i].name)) {
                held |= modifiers[i].modifier;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
        name += part + 1;
        length -= part + 1;
    }
    struct typed_key key = {0};
    bool shift = (held & INPUT_MOD_SHIFT) != 0;
    if (length == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9'))) {
        char c = name[0];
        if (shift && c >= 'a') {
            c = (char)(c - 'a' + 'A');
        }
        if (!typing_key_for_char((unsigned char)c, &key)) {
            return false;
        }
        if (shift && c >= '0' && c <= '9') {
            struct typed_key shifted_key;
            typing_key_for_char((unsigned char)shifted[c == '0' ? 9 : c - '1'], &shifted_key);
            key.codepoint = shifted_key.codepoint;
        }
    } else if (length > 1 && name[0] == 'f' && name[1] >= '1' && name[1] <= '9' &&
               (length == 2 || (length == 3 && name[1] == '1' && name[2] >= '0' && name[2] <= '2'))) {
        unsigned n = length == 2 ? (unsigned)(name[1] - '0') : 10 + (unsigned)(name[2] - '0');
        key.keycode = (uint16_t)(HID_KEY_F1 + n - 1);
    } else {
        bool found = false;
        for (unsigned i = 0; i < sizeof(named) / sizeof(named[0]); i++) {
            if (equal(name, length, named[i].name)) {
                key.keycode = named[i].usage;
                key.codepoint = named[i].usage == HID_KEY_SPACE ? ' ' : 0;
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }
    key.modifiers = (uint16_t)(key.modifiers | held);
    *out = key;
    return true;
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int typing_unescape(const char *text, char *out, size_t size) {
    size_t n = 0;
    for (const char *p = text; *p; p++) {
        char c = *p;
        if (c == '\\') {
            p++;
            switch (*p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'e': c = 0x1B; break;
            case 's': c = ' '; break;
            case '\\': c = '\\'; break;
            case 'x': {
                int high = hex(p[1]), low = high < 0 ? -1 : hex(p[2]);
                if (low < 0) {
                    return -1;
                }
                c = (char)(high * 16 + low);
                p += 2;
                break;
            }
            default: return -1;
            }
        }
        if (n == size) {
            return -1;
        }
        out[n++] = c;
    }
    return (int)n;
}

// --- The mouse ---------------------------------------------------------------

// The next word of `*text`, skipping spaces: its start and length (0 at the end).
static const char *word(const char **text, size_t *length) {
    const char *p = *text;
    while (*p == ' ') {
        p++;
    }
    const char *start = p;
    while (*p && *p != ' ') {
        p++;
    }
    *length = (size_t)(p - start);
    *text = p;
    return start;
}

// A decimal integer, with an optional '-', from min to max.
static bool integer(const char *text, size_t length, long min, long max, long *out) {
    size_t i = 0;
    bool negative = length > 0 && text[0] == '-';
    i += negative;
    if (i == length) {
        return false;
    }
    long value = 0;
    for (; i < length; i++) {
        if (text[i] < '0' || text[i] > '9' || value > 100000) {
            return false;
        }
        value = value * 10 + (text[i] - '0');
    }
    value = negative ? -value : value;
    if (value < min || value > max) {
        return false;
    }
    *out = value;
    return true;
}

static uint8_t button(const char *name, size_t length) {
    static const char *const names[] = {"left", "right", "middle", "side", "extra"};
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (equal(name, length, names[i])) {
            return (uint8_t)(1u << i);
        }
    }
    return 0;
}

unsigned typing_mouse(const char *args, uint8_t *buttons, struct input_event events[2]) {
    size_t length[4];
    const char *w[4];
    for (int i = 0; i < 4; i++) {
        w[i] = word(&args, &length[i]);
    }
    if (length[0] == 0 || length[3] != 0) {
        return 0;
    }
    struct input_event e = {.type = INPUT_EVENT_MOUSE, .buttons = *buttons};
    long a, b;
    if (equal(w[0], length[0], "to")) {
        if (!integer(w[1], length[1], 0, INPUT_ABS_MAX, &a) ||
            !integer(w[2], length[2], 0, INPUT_ABS_MAX, &b)) {
            return 0;
        }
        e.flags = INPUT_FLAG_ABSOLUTE;
        e.x = (int16_t)a;
        e.y = (int16_t)b;
        events[0] = e;
        return 1;
    }
    if (equal(w[0], length[0], "wheel")) {
        if (length[2] != 0 || !integer(w[1], length[1], -128, 127, &a)) {
            return 0;
        }
        e.wheel = (int8_t)a;
        events[0] = e;
        return 1;
    }
    bool down = equal(w[0], length[0], "down"), up = equal(w[0], length[0], "up");
    bool click = equal(w[0], length[0], "click");
    if (down || up || click) {
        uint8_t b_bit = button(w[1], length[1]);
        if (b_bit == 0 || length[2] != 0) {
            return 0;
        }
        e.buttons = up ? (uint8_t)(*buttons & ~b_bit) : (uint8_t)(*buttons | b_bit);
        events[0] = e;
        if (click) {
            events[1] = e;
            events[1].buttons = (uint8_t)(*buttons & ~b_bit);
        }
        *buttons = click ? events[1].buttons : e.buttons;
        return click ? 2 : 1;
    }
    if (!integer(w[0], length[0], -32768, 32767, &a) || !integer(w[1], length[1], -32768, 32767, &b) ||
        length[2] != 0) {
        return 0;
    }
    e.x = (int16_t)a;
    e.y = (int16_t)b;
    events[0] = e;
    return 1;
}
