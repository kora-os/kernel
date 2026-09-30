// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.h"
#include "drivers/virtio_input.h"

static struct virtio_keymap_state state;
static struct virtio_keymap_result result;

static void key(uint16_t code, uint32_t value) {
    virtio_keymap_event(&state, 1, code, value, &result);
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
    CHECK(!result.length && state.shift == 2, "right Shift survives left Shift release");
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
    CHECK(state.control == 2, "right Ctrl survives left Ctrl release");
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
    virtio_keymap_event(&state, 2, 30, 1, &result);
    CHECK(!result.length && state.shift, "pointer event ignored without corrupting modifiers");
    virtio_keymap_event(&state, 0, 0, 0, &result);
    CHECK(!result.length && state.shift, "EV_SYN report ignored");
    virtio_keymap_event(&state, 0, 3, 0, &result);
    CHECK(!state.shift && !state.control && !state.caps_down && !state.caps_lock,
          "dropped event stream resets stuck modifiers");
    character(30, 'a');
    virtio_keymap_event(NULL, 1, 30, 1, &result);
    CHECK(!result.length, "NULL state ignored");
    virtio_keymap_event(&state, 1, 30, 1, NULL);
}

TEST_MAIN(test_us_layout, test_modifiers_and_repeats, test_navigation_and_ignored_events)
