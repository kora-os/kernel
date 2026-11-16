// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// USB HID boot keyboard reports to key transitions. A report is a snapshot:
// a modifier byte and the usages of up to six held keys. This compares each
// one with the report before and names every key that went down or up. Pure
// logic, shared by the Circle USB keyboard glue and the host tests.

#ifdef __cplusplus
extern "C" {
#endif

#define HID_REPORT_KEYS 6

struct hid_keyboard {
    uint8_t modifiers;              // modifier byte of the last report
    uint8_t keys[HID_REPORT_KEYS];  // keys held in it, 0 in unused slots
};

// One key transition. `usage` is the key (0xE0 to 0xE7 for a modifier key),
// `modifiers` the modifier byte once the transition is done.
typedef void (*hid_key_fn)(void *ctx, uint8_t usage, bool down, uint8_t modifiers);

// Take the next report and call `key` for each transition: modifier keys
// first, then released keys, then pressed keys in report order. A report
// saying too many keys are held (the keyboard cannot tell which) keeps the
// keys of the report before and only updates the modifiers.
void hid_keyboard_report(struct hid_keyboard *keyboard, uint8_t modifiers,
                         const uint8_t keys[HID_REPORT_KEYS], hid_key_fn key, void *ctx);

#ifdef __cplusplus
}
#endif
