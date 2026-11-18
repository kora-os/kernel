// SPDX-License-Identifier: GPL-3.0-or-later
//
// HID boot keyboard reports to key transitions. See input/hid_keyboard.h.

#include "input/hid_keyboard.h"

#include "input/hid_keys.h"

static bool holds(const uint8_t keys[HID_REPORT_KEYS], unsigned count, uint8_t usage) {
    for (unsigned i = 0; i < count; i++) {
        if (keys[i] == usage) {
            return true;
        }
    }
    return false;
}

void hid_keyboard_report(struct hid_keyboard *keyboard, uint8_t modifiers,
                         const uint8_t keys[HID_REPORT_KEYS], hid_key_fn key, void *ctx) {
    uint8_t changed = keyboard->modifiers ^ modifiers;
    for (unsigned bit = 0; bit < 8; bit++) {
        uint8_t mask = (uint8_t)(1u << bit);
        if (changed & mask) {
            // One key at a time, so each event sees the keys changed so far.
            keyboard->modifiers ^= mask;
            key(ctx, (uint8_t)(HID_KEY_LEFT_CTRL + bit), (modifiers & mask) != 0,
                keyboard->modifiers);
        }
    }

    if (holds(keys, HID_REPORT_KEYS, HID_KEY_ROLLOVER)) {
        return;
    }
    for (unsigned i = 0; i < HID_REPORT_KEYS; i++) {
        uint8_t usage = keyboard->keys[i];
        if (usage != 0 && !holds(keys, HID_REPORT_KEYS, usage)) {
            key(ctx, usage, false, modifiers);
        }
    }
    uint8_t held[HID_REPORT_KEYS] = {0};
    for (unsigned i = 0; i < HID_REPORT_KEYS; i++) {
        uint8_t usage = keys[i];
        // Usages below HID_KEY_FIRST are error codes; a usage listed twice
        // is one key.
        if (usage < HID_KEY_FIRST || holds(held, i, usage)) {
            continue;
        }
        held[i] = usage;
        if (!holds(keyboard->keys, HID_REPORT_KEYS, usage)) {
            key(ctx, usage, true, modifiers);
        }
    }
    for (unsigned i = 0; i < HID_REPORT_KEYS; i++) {
        keyboard->keys[i] = held[i];
    }
}
