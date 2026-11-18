// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the VirtIO keyboard's key translation
// (src/drivers/virtio_keymap.c). The keymap makes input events; the byte
// checks run them through the tty adapter (src/input/input_tty.c), and their
// expectations are the bytes the keymap produced itself before events
// existed: the two together must still type the same thing.

#include "test.h"
#include "drivers/virtio_input.h"
#include "input/hid_keys.h"
#include "input/input_tty.h"

static struct virtio_keymap_state state;
static struct input_event event;
static bool produced;
static struct input_tty_bytes result;

// One device event through the keymap and, if it made a key event, the adapter.
static void device(uint16_t type, uint16_t code, uint32_t value) {
    result = (struct input_tty_bytes){0};
    produced = virtio_keymap_event(&state, type, code, value, &event);
    if (produced) {
        input_tty_translate(&event, &result);
    }
}
static void key(uint16_t code, uint32_t value) {
    device(1, code, value);
}
static void character(uint16_t code, char expected) {
    key(code, 1);
    CHECK(result.length == 1 && result.bytes[0] == expected && !result.scroll,
          "key%u expected character0x%x", code, (unsigned char)expected);
}
static void reset(void) {
    state = (struct virtio_keymap_state){0};
}

static void test_us_layout(void) {
    reset();
    const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    const uint16_t codes[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    for (unsigned i = 0; i < 26; i++) character(codes[i], letters[i]);
    const char digits[] = "1234567890";
    for (unsigned i = 0; i < 10; i++) character(2 + i, digits[i]);
    const uint16_t punctuation[] = {12,13,26,27,39,40,41,43,51,52,53};
    const char plain[] = "-=[];'`\\,./";
    const char shifted[] = "_+{}:\"~|<>?";
    for (unsigned i = 0; i < 11; i++) character(punctuation[i], plain[i]);
    key(42, 1);
    for (unsigned i = 0; i < 26; i++) character(codes[i], letters[i] - ('a' - 'A'));
    const char shift_digits[] = "!@#$%^&*()";
    for (unsigned i = 0; i < 10; i++) character(2 + i, shift_digits[i]);
    for (unsigned i = 0; i < 11; i++) character(punctuation[i], shifted[i]);
    character(28, '\n');
    character(15, '\t');
    character(14, 0x7F);
    character(57, ' ');
    character(1, 0x1B);
}

static void test_modifiers_and_repeats(void) {
    reset();
    key(42, 1); key(54, 1); key(42, 0);
    CHECK(!result.length && state.modifiers == INPUT_MOD_RIGHT_SHIFT,
          "right Shift survives left Shift release");
    character(30, 'A');
    key(54, 0); character(30, 'a');
    key(58, 1); character(30, 'A');
    key(58, 2); key(58, 1); character(30, 'A');
    CHECK(state.caps_lock, "Caps repeats and duplicate presses do not toggle");
    key(42, 1); character(30, 'a'); character(2, '!');
    key(42, 0); character(2, '1');
    key(58, 0); key(58, 1); character(30, 'a');
    key(30, 2);
    CHECK(result.length == 1 && result.bytes[0] == 'a', "key repeat produces text");
    key(30, 0);
    CHECK(!result.length && !result.scroll, "ordinary key release produces no text");
    key(29, 1); key(97, 1); key(29, 0);
    character(46, 3);
    key(42, 1); key(58, 0); key(58, 1); character(44, 26);
    CHECK(state.modifiers == (INPUT_MOD_RIGHT_CTRL | INPUT_MOD_LEFT_SHIFT),
          "right Ctrl survives left Ctrl release");
    key(97, 0); key(42, 0); character(44, 'Z');
    key(97, 1); character(57, 0);
    character(3, 0); character(7, 0x1E); character(12, 0x1F);
    character(26, 0x1B); character(43, 0x1C); character(27, 0x1D);
    key(42, 1);
    character(26, 0x1B); character(43, 0x1C); character(27, 0x1D);
}

static void expect_sequence(uint16_t code, const char *expected) {
    key(code, 1);
    unsigned length = 0;
    while (expected[length]) length++;
    CHECK(result.length == length && !result.scroll, "key%u sequence length", code);
    for (unsigned i = 0; i < length; i++) {
        CHECK(result.bytes[i] == expected[i], "key%u sequence byte%u", code, i);
    }
}
static void test_navigation_and_ignored_events(void) {
    reset();
    expect_sequence(103, "\x1B[A"); expect_sequence(108, "\x1B[B");
    expect_sequence(106, "\x1B[C"); expect_sequence(105, "\x1B[D");
    expect_sequence(102, "\x1B[H"); expect_sequence(107, "\x1B[F");
    expect_sequence(104, "\x1B[5~"); expect_sequence(109, "\x1B[6~");
    expect_sequence(110, "\x1B[2~"); expect_sequence(111, "\x1B[3~");
    key(42, 1); key(104, 1);
    CHECK(result.scroll == 1 && !result.length, "Shift+PageUp scrolls history");
    key(109, 2);
    CHECK(result.scroll == -1 && !result.length, "Shift+PageDown repeat scrolls forward");
    key(109, 0); CHECK(!result.scroll && !result.length, "scroll key release ignored");
    key(65535, 1); CHECK(!result.length && !result.scroll, "out-of-range code ignored");
    key(59, 1); CHECK(!result.length, "unmapped F-key ignored");
    key(30, 3); CHECK(!result.length, "invalid EV_KEY value ignored");
    device(2, 30, 1);
    CHECK(!produced && state.modifiers, "pointer event ignored without corrupting modifiers");
    device(0, 0, 0);
    CHECK(!produced && state.modifiers, "EV_SYN report ignored");
    device(0, 3, 0);
    CHECK(!produced && !state.modifiers && !state.caps_down && !state.caps_lock,
          "dropped event stream resets stuck modifiers");
    character(30, 'a');
    CHECK(!virtio_keymap_event(NULL, 1, 30, 1, &event), "NULL state ignored");
    CHECK(!virtio_keymap_event(&state, 1, 30, 1, NULL), "NULL event ignored");
}

// The events themselves: what the queue carries, before any adapter.
static bool is(uint8_t type, uint8_t flags, uint16_t modifiers, uint16_t keycode,
               uint32_t codepoint) {
    return produced && event.type == type && event.flags == flags &&
           event.modifiers == modifiers && event.keycode == keycode &&
           event.codepoint == codepoint && event.reserved == 0;
}
#define DOWN INPUT_EVENT_KEY_DOWN
#define UP INPUT_EVENT_KEY_UP

static void test_events(void) {
    reset();
    key(30, 1);
    CHECK(is(DOWN, 0, 0, HID_KEY_A, 'a'), "a key going down, with its position and character");
    key(30, 2);
    CHECK(is(DOWN, INPUT_FLAG_REPEAT, 0, HID_KEY_A, 'a'), "a repeat is a flagged key down");
    key(30, 0);
    CHECK(is(UP, 0, 0, HID_KEY_A, 0), "a key going up carries no character");

    // The eight modifier keys are keys too, and each sets its own bit.
    static const uint16_t codes[8] = {29, 42, 56, 125, 97, 54, 100, 126};
    uint16_t held = 0;
    for (unsigned bit = 0; bit < 8; bit++) {
        held |= (uint16_t)(1u << bit);
        key(codes[bit], 1);
        CHECK(is(DOWN, 0, held, (uint16_t)(HID_KEY_LEFT_CTRL + bit), 0),
              "modifier %u down is key 0x%x and sets its bit", bit, HID_KEY_LEFT_CTRL + bit);
    }
    for (unsigned bit = 0; bit < 8; bit++) {
        held &= (uint16_t)~(1u << bit);
        key(codes[bit], 0);
        CHECK(is(UP, 0, held, (uint16_t)(HID_KEY_LEFT_CTRL + bit), 0),
              "modifier %u up clears its bit", bit);
    }

    key(42, 1);
    key(30, 1);
    CHECK(is(DOWN, 0, INPUT_MOD_LEFT_SHIFT, HID_KEY_A, 'A'), "Shift picks the character");
    key(42, 0);
    key(58, 1);
    CHECK(is(DOWN, 0, INPUT_MOD_CAPS_LOCK, HID_KEY_CAPS_LOCK, 0), "Caps Lock is a key and a lock");
    key(58, 0);
    CHECK(is(UP, 0, INPUT_MOD_CAPS_LOCK, HID_KEY_CAPS_LOCK, 0), "the lock outlives the key");
    key(30, 1);
    CHECK(is(DOWN, 0, INPUT_MOD_CAPS_LOCK, HID_KEY_A, 'A'), "Caps Lock picks the character");
    key(58, 1); key(58, 0);

    // Ctrl, Alt and GUI are reported, and leave the character to the consumer.
    key(29, 1);
    key(46, 1);
    CHECK(is(DOWN, 0, INPUT_MOD_LEFT_CTRL, 0x06, 'c'), "Ctrl+C is the C key, 'c' and Ctrl");
    key(29, 0);
    key(56, 1);
    key(46, 1);
    CHECK(is(DOWN, 0, INPUT_MOD_LEFT_ALT, 0x06, 'c'), "Alt+C is the C key, 'c' and Alt");
    key(56, 0);

    // Keys that type nothing: known by position alone.
    static const struct {
        uint16_t code;
        uint16_t usage;
    } plain[] = {
        {28, HID_KEY_ENTER}, {1, HID_KEY_ESCAPE}, {14, HID_KEY_BACKSPACE}, {15, HID_KEY_TAB},
        {96, HID_KEY_KP_ENTER}, {59, HID_KEY_F1}, {88, HID_KEY_F12}, {110, HID_KEY_INSERT},
        {102, HID_KEY_HOME}, {104, HID_KEY_PAGE_UP}, {111, HID_KEY_DELETE}, {107, HID_KEY_END},
        {109, HID_KEY_PAGE_DOWN}, {106, HID_KEY_RIGHT}, {105, HID_KEY_LEFT},
        {108, HID_KEY_DOWN}, {103, HID_KEY_UP}, {69, HID_KEY_NUM_LOCK}
    };
    for (unsigned i = 0; i < sizeof(plain) / sizeof(plain[0]); i++) {
        key(plain[i].code, 1);
        CHECK(is(DOWN, 0, 0, plain[i].usage, 0), "evdev %u is key 0x%x, no character",
              plain[i].code, plain[i].usage);
    }
    key(57, 1);
    CHECK(is(DOWN, 0, 0, HID_KEY_SPACE, ' '), "Space types a space");

    // Letters run in alphabetical order in the usage table.
    static const uint16_t letters[] = {30,48,46,32,18,33,34,35,23,36,37,38,50,49,24,25,16,19,31,20,22,47,17,45,21,44};
    for (unsigned i = 0; i < 26; i++) {
        key(letters[i], 1);
        CHECK(is(DOWN, 0, 0, (uint16_t)(HID_KEY_A + i), (uint32_t)('a' + i)), "letter %c", 'a' + i);
    }
    // Every usage is given to one evdev code only.
    static uint8_t used[256];
    for (uint16_t code = 0; code < 256; code++) {
        key(code, 1);
        if (produced) {
            CHECK(event.keycode >= HID_KEY_FIRST && event.keycode < 256 && !used[event.keycode]++,
                  "evdev %u has a key code of its own (0x%x)", code, event.keycode);
        }
        key(code, 0);
    }
    key(84, 1);
    CHECK(!produced, "an evdev code that is no key makes no event");
}

static void test_rejected_event(void) {
    reset();
    struct input_event untouched = {.type = 0x55, .keycode = 0x1234};
    CHECK(!virtio_keymap_event(&state, 1, 30, 3, &untouched) &&
          untouched.type == 0x55 && untouched.keycode == 0x1234,
          "a rejected device event leaves the caller's event alone");
}

TEST_MAIN(test_us_layout, test_modifiers_and_repeats, test_navigation_and_ignored_events,
          test_events, test_rejected_event)
