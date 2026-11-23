// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// Initialize one modern virtio MMIO keyboard discovered by the platform DTB.
// Its interrupt handler posts key events to the input queue (input/input.h);
// failure leaves the machine without a keyboard.
bool virtio_input_init(void);

// Pure evdev key translation with a US layout, shared with host tests: Linux
// evdev key codes in, input events out.
struct virtio_keymap_state {
    uint8_t modifiers;  // held modifier keys, as the low byte of INPUT_MOD_*
    bool caps_lock;
    bool caps_down;
};
// Feed one device event (type, code, value). Returns true when it is a key
// going down, repeating or going up, with `event` filled (but for its time);
// false for everything else, `event` untouched.
bool virtio_keymap_event(struct virtio_keymap_state *state, uint16_t type,
                         uint16_t code, uint32_t value, struct input_event *event);
