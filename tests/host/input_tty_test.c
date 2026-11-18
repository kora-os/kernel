// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the key event to console byte adapter
// (src/input/input_tty.c). The bytes it must produce are the ones the
// keyboard drivers pushed to the tty before events existed.

#include "test.h"
#include "input/hid_keys.h"
#include "input/input_tty.h"

static struct input_tty_bytes out;

static void down(uint16_t keycode, uint16_t modifiers, uint32_t codepoint) {
    struct input_event event = {
        .type = INPUT_EVENT_KEY_DOWN, .modifiers = modifiers,
        .keycode = keycode, .codepoint = codepoint
    };
    out = (struct input_tty_bytes){.length = 99, .scroll = 99};
    input_tty_translate(&event, &out);
}

static bool sent(const char *expected, unsigned length) {
    if (out.length != length || out.scroll != 0) {
        return false;
    }
    for (unsigned i = 0; i < length; i++) {
        if (out.bytes[i] != expected[i]) {
            return false;
        }
    }
    return true;
}
#define SENT(string) sent(string, sizeof(string) - 1)

static void test_characters(void) {
    down(HID_KEY_A, 0, 'a');
    CHECK(SENT("a"), "a key sends its character");
    down(HID_KEY_A, INPUT_MOD_LEFT_SHIFT, 'A');
    CHECK(SENT("A"), "Shift is already in the character");
    down(HID_KEY_A, INPUT_MOD_CAPS_LOCK | INPUT_MOD_NUM_LOCK, 'A');
    CHECK(SENT("A"), "locks are already in the character");
    down(HID_KEY_SPACE, 0, ' ');
    CHECK(SENT(" "), "Space is a character");
    down(0x08, INPUT_MOD_RIGHT_ALT, 0xE9);
    CHECK(SENT("\xE9"), "a character below 0x100 is one byte");
    down(0x22, INPUT_MOD_RIGHT_ALT, 0x20AC);
    CHECK(SENT(""), "a character above 0xFF has no byte");
    down(HID_KEY_A, INPUT_MOD_LEFT_ALT, 'a');
    CHECK(SENT("a"), "Alt changes nothing");
    down(HID_KEY_A, INPUT_MOD_RIGHT_GUI, 'a');
    CHECK(SENT("a"), "GUI changes nothing");
    down(HID_KEY_F1, 0, 0);
    CHECK(SENT(""), "a key without a character sends nothing");
    down(HID_KEY_LEFT_CTRL + 1, INPUT_MOD_LEFT_SHIFT, 0);
    CHECK(SENT(""), "a modifier key sends nothing");
}

static void test_named_keys(void) {
    down(HID_KEY_ENTER, 0, 0);
    CHECK(SENT("\n"), "Enter");
    down(HID_KEY_KP_ENTER, 0, 0);
    CHECK(SENT("\n"), "keypad Enter");
    down(HID_KEY_TAB, 0, 0);
    CHECK(SENT("\t"), "Tab");
    down(HID_KEY_BACKSPACE, 0, 0);
    CHECK(SENT("\x7F"), "Backspace");
    down(HID_KEY_ESCAPE, 0, 0);
    CHECK(SENT("\x1B"), "Escape");
    down(HID_KEY_ENTER, INPUT_MOD_LEFT_CTRL | INPUT_MOD_LEFT_SHIFT, 0);
    CHECK(SENT("\n"), "modifiers leave Enter alone");
    down(HID_KEY_BACKSPACE, INPUT_MOD_RIGHT_CTRL, 0);
    CHECK(SENT("\x7F"), "Ctrl leaves Backspace alone");

    static const struct {
        uint16_t keycode;
        const char *bytes;
    } keys[] = {
        {HID_KEY_UP, "\x1B[A"}, {HID_KEY_DOWN, "\x1B[B"}, {HID_KEY_RIGHT, "\x1B[C"},
        {HID_KEY_LEFT, "\x1B[D"}, {HID_KEY_HOME, "\x1B[H"}, {HID_KEY_END, "\x1B[F"},
        {HID_KEY_PAGE_UP, "\x1B[5~"}, {HID_KEY_PAGE_DOWN, "\x1B[6~"},
        {HID_KEY_INSERT, "\x1B[2~"}, {HID_KEY_DELETE, "\x1B[3~"}
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        unsigned length = 0;
        while (keys[i].bytes[length]) {
            length++;
        }
        CHECK(length <= INPUT_TTY_MAX_BYTES, "sequence %u fits the result", i);
        down(keys[i].keycode, 0, 0);
        CHECK(sent(keys[i].bytes, length), "key 0x%x sends its xterm sequence", keys[i].keycode);
        down(keys[i].keycode, INPUT_MOD_LEFT_CTRL | INPUT_MOD_LEFT_ALT, 0);
        CHECK(sent(keys[i].bytes, length), "Ctrl and Alt leave key 0x%x alone", keys[i].keycode);
    }
}

static void test_control(void) {
    for (char c = 'a'; c <= 'z'; c++) {
        down((uint16_t)(HID_KEY_A + c - 'a'), INPUT_MOD_LEFT_CTRL, (uint32_t)c);
        CHECK(out.length == 1 && out.bytes[0] == c - 'a' + 1, "Ctrl+%c", c);
        down((uint16_t)(HID_KEY_A + c - 'a'), INPUT_MOD_RIGHT_CTRL | INPUT_MOD_LEFT_SHIFT,
             (uint32_t)(c - 'a' + 'A'));
        CHECK(out.length == 1 && out.bytes[0] == c - 'a' + 1, "Ctrl+Shift+%c", c);
    }
    static const struct {
        char character;
        char sent;
    } folded[] = {
        {' ', 0}, {'2', 0}, {'@', 0}, {'[', 0x1B}, {'{', 0x1B}, {'\\', 0x1C}, {'|', 0x1C},
        {']', 0x1D}, {'}', 0x1D}, {'6', 0x1E}, {'^', 0x1E}, {'-', 0x1F}, {'_', 0x1F},
        {'/', 0x1F}, {'?', 0x7F},
        {'1', '1'}, {'=', '='}, {'`', '`'}, {';', ';'}  // no control code: unchanged
    };
    for (unsigned i = 0; i < sizeof(folded) / sizeof(folded[0]); i++) {
        down(0x64, INPUT_MOD_LEFT_CTRL, (uint32_t)folded[i].character);
        CHECK(out.length == 1 && out.bytes[0] == folded[i].sent && out.scroll == 0,
              "Ctrl+'%c' sends 0x%x", folded[i].character, (unsigned char)out.bytes[0]);
    }
    down(0x08, INPUT_MOD_LEFT_CTRL | INPUT_MOD_RIGHT_ALT, 0xE9);
    CHECK(SENT(""), "Ctrl with a character outside ASCII sends nothing");
}

static void test_scrollback(void) {
    down(HID_KEY_PAGE_UP, INPUT_MOD_LEFT_SHIFT, 0);
    CHECK(out.length == 0 && out.scroll == 1, "Shift+PageUp scrolls back");
    down(HID_KEY_PAGE_DOWN, INPUT_MOD_RIGHT_SHIFT, 0);
    CHECK(out.length == 0 && out.scroll == -1, "Shift+PageDown scrolls forward");
    down(HID_KEY_PAGE_UP, INPUT_MOD_SHIFT | INPUT_MOD_LEFT_CTRL, 0);
    CHECK(out.length == 0 && out.scroll == 1, "Ctrl does not stop the scroll");
}

static void test_up_and_repeat(void) {
    struct input_event event = {
        .type = INPUT_EVENT_KEY_UP, .modifiers = INPUT_MOD_LEFT_SHIFT,
        .keycode = HID_KEY_PAGE_UP, .codepoint = 'x'
    };
    out = (struct input_tty_bytes){.length = 99, .scroll = 99};
    input_tty_translate(&event, &out);
    CHECK(out.length == 0 && out.scroll == 0, "a key going up sends nothing");

    event = (struct input_event){
        .type = INPUT_EVENT_KEY_DOWN, .flags = INPUT_FLAG_REPEAT,
        .keycode = HID_KEY_A, .codepoint = 'a'
    };
    input_tty_translate(&event, &out);
    CHECK(SENT("a"), "a repeat sends the character again");

    event = (struct input_event){.type = 0x7F, .keycode = HID_KEY_A, .codepoint = 'a'};
    input_tty_translate(&event, &out);
    CHECK(out.length == 0 && out.scroll == 0, "an event that is not a key sends nothing");
}

TEST_MAIN(test_characters, test_named_keys, test_control, test_scrollback, test_up_and_repeat)
