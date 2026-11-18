// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the HID boot report comparison (src/input/hid_keyboard.c):
// the key transitions the Pi's USB keyboard path posts as events.

#include "test.h"
#include "input/hid_keyboard.h"

static struct hid_keyboard keyboard;

static struct {
    uint8_t usage;
    bool down;
    uint8_t modifiers;
} seen[32];
static unsigned count;
static int context;

static void record(void *ctx, uint8_t usage, bool down, uint8_t modifiers) {
    CHECK(ctx == &context, "the context reaches the callback");
    if (count < sizeof(seen) / sizeof(seen[0])) {
        seen[count].usage = usage;
        seen[count].down = down;
        seen[count].modifiers = modifiers;
    }
    count++;
}

static void report(uint8_t modifiers, uint8_t k0, uint8_t k1, uint8_t k2,
                   uint8_t k3, uint8_t k4, uint8_t k5) {
    const uint8_t keys[HID_REPORT_KEYS] = {k0, k1, k2, k3, k4, k5};
    count = 0;
    hid_keyboard_report(&keyboard, modifiers, keys, record, &context);
}

static bool was(unsigned index, uint8_t usage, bool down, uint8_t modifiers) {
    return index < count && seen[index].usage == usage && seen[index].down == down &&
           seen[index].modifiers == modifiers;
}

static void test_press_and_release(void) {
    keyboard = (struct hid_keyboard){0};
    report(0, 0, 0, 0, 0, 0, 0);
    CHECK(count == 0, "an idle report is no event");
    report(0, 0x04, 0, 0, 0, 0, 0);
    CHECK(count == 1 && was(0, 0x04, true, 0), "a key appearing went down");
    report(0, 0x04, 0, 0, 0, 0, 0);
    CHECK(count == 0, "a held key is no event");
    report(0, 0x04, 0x05, 0, 0, 0, 0);
    CHECK(count == 1 && was(0, 0x05, true, 0), "a second key goes down beside the first");
    report(0, 0x05, 0, 0, 0, 0, 0);
    CHECK(count == 1 && was(0, 0x04, false, 0), "a key moving slots is not pressed again");
    report(0, 0x06, 0x07, 0, 0, 0, 0);
    CHECK(count == 3 && was(0, 0x05, false, 0) && was(1, 0x06, true, 0) &&
          was(2, 0x07, true, 0), "releases come before presses, presses in report order");
    report(0, 0, 0, 0, 0, 0, 0);
    CHECK(count == 2 && was(0, 0x06, false, 0) && was(1, 0x07, false, 0), "all keys released");
}

static void test_modifiers(void) {
    keyboard = (struct hid_keyboard){0};
    report(0x02, 0, 0, 0, 0, 0, 0);
    CHECK(count == 1 && was(0, 0xE1, true, 0x02), "left Shift is key 0xE1, already held");
    report(0x02, 0x04, 0, 0, 0, 0, 0);
    CHECK(count == 1 && was(0, 0x04, true, 0x02), "a key carries the held modifiers");
    report(0x21, 0x04, 0, 0, 0, 0, 0);
    CHECK(count == 3 && was(0, 0xE0, true, 0x03) && was(1, 0xE1, false, 0x01) &&
          was(2, 0xE5, true, 0x21),
          "modifiers change one at a time, lowest bit first, each seeing the ones before");
    report(0x80, 0x05, 0, 0, 0, 0, 0);
    CHECK(count == 5 && was(0, 0xE0, false, 0x20) && was(1, 0xE5, false, 0x00) &&
          was(2, 0xE7, true, 0x80) && was(3, 0x04, false, 0x80) && was(4, 0x05, true, 0x80),
          "modifiers first, then releases, then presses, all with the new modifiers");
    report(0, 0, 0, 0, 0, 0, 0);
    CHECK(count == 2 && was(0, 0xE7, false, 0) && was(1, 0x05, false, 0), "everything released");
    for (unsigned bit = 0; bit < 8; bit++) {
        report(0, 0, 0, 0, 0, 0, 0);
        report((uint8_t)(1u << bit), 0, 0, 0, 0, 0, 0);
        CHECK(count == 1 && was(0, (uint8_t)(0xE0 + bit), true, (uint8_t)(1u << bit)),
              "modifier bit %u is key 0x%x", bit, 0xE0 + bit);
    }
}

static void test_errors(void) {
    keyboard = (struct hid_keyboard){0};
    report(0, 0x04, 0x05, 0, 0, 0, 0);
    report(0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01);
    CHECK(count == 1 && was(0, 0xE1, true, 0x02),
          "a rollover report releases nothing and still tracks modifiers");
    report(0x02, 0x04, 0x05, 0x06, 0, 0, 0);
    CHECK(count == 1 && was(0, 0x06, true, 0x02),
          "after the rollover the held keys are not pressed again");
    report(0x02, 0x02, 0x03, 0x06, 0, 0, 0);
    CHECK(count == 2 && was(0, 0x04, false, 0x02) && was(1, 0x05, false, 0x02),
          "other error codes are not keys");
    report(0x02, 0x06, 0x07, 0x07, 0x06, 0, 0);
    CHECK(count == 1 && was(0, 0x07, true, 0x02), "a key listed twice goes down once");
    report(0x02, 0x06, 0x07, 0, 0, 0, 0);
    CHECK(count == 0, "and is still held when listed once");
    report(0, 0xFF, 0, 0, 0, 0, 0);
    CHECK(count == 4 && was(0, 0xE1, false, 0) && was(1, 0x06, false, 0) &&
          was(2, 0x07, false, 0) && was(3, 0xFF, true, 0), "any usage from 4 up is a key");
}

TEST_MAIN(test_press_and_release, test_modifiers, test_errors)
