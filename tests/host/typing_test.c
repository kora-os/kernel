// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the debug console's keys from text (src/input/typing.c):
// what each character and each key name presses on a US keyboard, and the
// escapes of `type`.

#include "test.h"
#include "input/event.h"
#include "input/hid_keys.h"
#include "input/typing.h"

static int is(const struct typed_key *k, unsigned keycode, unsigned modifiers, unsigned codepoint) {
    return k->keycode == keycode && k->modifiers == modifiers && k->codepoint == codepoint;
}

#define CHAR_IS(c, want_key, want_mods, want_char)                                          \
    do {                                                                                   \
        struct typed_key k = {0};                                                          \
        CHECK(typing_key_for_char((unsigned char)(c), &k) && is(&k, want_key, want_mods, want_char), \
              "char 0x%02x: key 0x%02x mods 0x%04x char 0x%x", (unsigned)(unsigned char)(c),    \
              k.keycode, k.modifiers, (unsigned)k.codepoint);                                \
    } while (0)

#define NAME_IS(name, want_key, want_mods, want_char)                                       \
    do {                                                                                   \
        struct typed_key k = {0};                                                          \
        CHECK(typing_key_for_name(name, sizeof(name) - 1, &k) && is(&k, want_key, want_mods, want_char), \
              "%s: key 0x%02x mods 0x%04x char 0x%x", name, k.keycode, k.modifiers,         \
              (unsigned)k.codepoint);                                                       \
    } while (0)

static void test_characters(void) {
    CHAR_IS('a', HID_KEY_A, 0, 'a');
    CHAR_IS('z', HID_KEY_Z, 0, 'z');
    CHAR_IS('Q', 0x14, INPUT_MOD_LEFT_SHIFT, 'Q');
    CHAR_IS('1', 0x1E, 0, '1');
    CHAR_IS('0', 0x27, 0, '0');
    CHAR_IS('!', 0x1E, INPUT_MOD_LEFT_SHIFT, '!');
    CHAR_IS('#', 0x20, INPUT_MOD_LEFT_SHIFT, '#');
    CHAR_IS(')', 0x27, INPUT_MOD_LEFT_SHIFT, ')');
    CHAR_IS(' ', HID_KEY_SPACE, 0, ' ');
    CHAR_IS('-', 0x2D, 0, '-');
    CHAR_IS('_', 0x2D, INPUT_MOD_LEFT_SHIFT, '_');
    CHAR_IS('\\', 0x31, 0, '\\');
    CHAR_IS('|', 0x31, INPUT_MOD_LEFT_SHIFT, '|');
    CHAR_IS(';', 0x33, 0, ';');
    CHAR_IS('"', 0x34, INPUT_MOD_LEFT_SHIFT, '"');
    CHAR_IS('`', 0x35, 0, '`');
    CHAR_IS('~', 0x35, INPUT_MOD_LEFT_SHIFT, '~');
    CHAR_IS('/', 0x38, 0, '/');
    CHAR_IS('?', 0x38, INPUT_MOD_LEFT_SHIFT, '?');
    CHAR_IS('>', 0x37, INPUT_MOD_LEFT_SHIFT, '>');
    CHAR_IS('\n', HID_KEY_ENTER, 0, 0);
    CHAR_IS('\t', HID_KEY_TAB, 0, 0);
    CHAR_IS(0x1b, HID_KEY_ESCAPE, 0, 0);
    CHAR_IS(0x7f, HID_KEY_BACKSPACE, 0, 0);
    CHAR_IS(0x03, 0x06, INPUT_MOD_LEFT_CTRL, 'c');
    CHAR_IS(0x04, 0x07, INPUT_MOD_LEFT_CTRL, 'd');
    CHAR_IS(0x00, HID_KEY_SPACE, INPUT_MOD_LEFT_CTRL, ' ');
    CHAR_IS(0x1c, 0x31, INPUT_MOD_LEFT_CTRL, '\\');
    CHAR_IS(0x1e, 0x23, INPUT_MOD_LEFT_CTRL | INPUT_MOD_LEFT_SHIFT, '^');
    struct typed_key k;
    CHECK(!typing_key_for_char(0x80, &k) && !typing_key_for_char(0xe9, &k),
          "no key types a byte past ASCII");
}

static void test_names(void) {
    NAME_IS("a", HID_KEY_A, 0, 'a');
    NAME_IS("shift-a", HID_KEY_A, INPUT_MOD_LEFT_SHIFT, 'A');
    NAME_IS("5", 0x22, 0, '5');
    NAME_IS("shift-5", 0x22, INPUT_MOD_LEFT_SHIFT, '%');
    NAME_IS("shift-0", 0x27, INPUT_MOD_LEFT_SHIFT, ')');
    NAME_IS("ctrl-c", 0x06, INPUT_MOD_LEFT_CTRL, 'c');
    NAME_IS("ctrl-shift-c", 0x06, INPUT_MOD_LEFT_CTRL | INPUT_MOD_LEFT_SHIFT, 'C');
    NAME_IS("meta_l-m", 0x10, INPUT_MOD_LEFT_GUI, 'm');
    NAME_IS("meta-m", 0x10, INPUT_MOD_LEFT_GUI, 'm');
    NAME_IS("meta_r-m", 0x10, INPUT_MOD_RIGHT_GUI, 'm');
    NAME_IS("alt-x", 0x1B, INPUT_MOD_LEFT_ALT, 'x');
    NAME_IS("ret", HID_KEY_ENTER, 0, 0);
    NAME_IS("spc", HID_KEY_SPACE, 0, ' ');
    NAME_IS("esc", HID_KEY_ESCAPE, 0, 0);
    NAME_IS("backspace", HID_KEY_BACKSPACE, 0, 0);
    NAME_IS("shift-pgup", HID_KEY_PAGE_UP, INPUT_MOD_LEFT_SHIFT, 0);
    NAME_IS("up", HID_KEY_UP, 0, 0);
    NAME_IS("home", HID_KEY_HOME, 0, 0);
    NAME_IS("delete", HID_KEY_DELETE, 0, 0);
    NAME_IS("f1", HID_KEY_F1, 0, 0);
    NAME_IS("f9", HID_KEY_F1 + 8, 0, 0);
    NAME_IS("f10", HID_KEY_F1 + 9, 0, 0);
    NAME_IS("f12", HID_KEY_F12, 0, 0);
    struct typed_key k;
    CHECK(!typing_key_for_name("f13", 3, &k) && !typing_key_for_name("f0", 2, &k),
          "no f13 or f0");
    CHECK(!typing_key_for_name("hyper-a", 7, &k), "an unknown modifier");
    CHECK(!typing_key_for_name("ctrl-", 5, &k) && !typing_key_for_name("", 0, &k) &&
              !typing_key_for_name("-", 1, &k), "no key");
    CHECK(!typing_key_for_name("enter", 5, &k) && !typing_key_for_name("A", 1, &k),
          "only the names listed (ret, and shift-a for A)");
    CHECK(typing_key_for_name("ret garbage", 3, &k) && k.keycode == HID_KEY_ENTER,
          "the length bounds the name");
}

static void test_escapes(void) {
    char out[32];
    int n = typing_unescape("ls /bin\\n", out, sizeof(out));
    CHECK(n == 8 && out[7] == '\n' && out[0] == 'l', "\\n is Enter (%d)", n);
    n = typing_unescape("\\sa\\tb\\e\\\\\\x03\\x7F", out, sizeof(out));
    CHECK(n == 8 && out[0] == ' ' && out[1] == 'a' && out[2] == '\t' && out[4] == 0x1b &&
              out[5] == '\\' && out[6] == 0x03 && out[7] == 0x7f, "every escape (%d)", n);
    CHECK(typing_unescape("\\q", out, sizeof(out)) == -1, "an unknown escape");
    CHECK(typing_unescape("\\x4", out, sizeof(out)) == -1 &&
              typing_unescape("\\xg0", out, sizeof(out)) == -1 &&
              typing_unescape("ab\\", out, sizeof(out)) == -1, "broken escapes");
    CHECK(typing_unescape("abcd", out, 3) == -1, "too long for the buffer");
    CHECK(typing_unescape("", out, sizeof(out)) == 0, "nothing");
}

TEST_MAIN(test_characters, test_names, test_escapes)
