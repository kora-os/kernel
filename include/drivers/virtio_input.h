// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// Initialize the modern VirtIO MMIO input devices the platform DTB lists: one
// keyboard, and pointers (a mouse, relative, or a tablet, absolute), up to
// VIRTIO_INPUT_DEVICES in all. Their interrupt handlers post events to the
// input queue (input/input.h). Returns whether a keyboard is there; *mice and
// *tablets (if given) count the pointers.
#define VIRTIO_INPUT_DEVICES 3
bool virtio_input_init(void);
void virtio_input_pointers(unsigned *mice, unsigned *tablets);

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

// Pure evdev pointer translation, shared with host tests: a mouse's movements,
// buttons and wheel, or a tablet's position, gathered up to each SYN_REPORT.
struct virtio_pointer_state {
    bool absolute;                    // a tablet: positions, not movements
    int32_t min_x, max_x, min_y, max_y;  // a tablet's range
    int32_t dx, dy, wheel;            // movement since the last report
    int16_t x, y;                     // a tablet's position, 0..INPUT_ABS_MAX
    uint8_t buttons;                  // INPUT_BUTTON_* held
    bool changed;                     // something to report
};
void virtio_pointer_init(struct virtio_pointer_state *state, bool absolute, int32_t min_x,
                         int32_t max_x, int32_t min_y, int32_t max_y);
// Feed one device event. Returns true at a report (EV_SYN) with something in
// it, `event` filled as an INPUT_EVENT_MOUSE (but for its time and source);
// false otherwise, `event` untouched.
bool virtio_pointer_event(struct virtio_pointer_state *state, uint16_t type, uint16_t code,
                          uint32_t value, struct input_event *event);
