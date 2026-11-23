// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_input.h"

// Linux evdev, as a VirtIO input device reports it.
#define EV_SYN 0
#define EV_KEY 1
#define EV_REL 2
#define EV_ABS 3
#define SYN_REPORT 0
#define REL_X 0
#define REL_Y 1
#define REL_WHEEL 8
#define ABS_X 0
#define ABS_Y 1
#define BTN_LEFT 0x110   // then right, middle, side and extra, as INPUT_BUTTON_*
#define BTN_EXTRA 0x114

void virtio_pointer_init(struct virtio_pointer_state *state, bool absolute, int32_t min_x,
                         int32_t max_x, int32_t min_y, int32_t max_y) {
    *state = (struct virtio_pointer_state){
        .absolute = absolute, .min_x = min_x, .max_x = max_x, .min_y = min_y, .max_y = max_y};
}

static int16_t clamp16(int32_t v) {
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

// A device position to 0..INPUT_ABS_MAX across its range.
static int16_t scale(int32_t value, int32_t min, int32_t max) {
    if (max <= min) {
        return 0;
    }
    if (value <= min) {
        return 0;
    }
    if (value >= max) {
        return INPUT_ABS_MAX;
    }
    return (int16_t)((int64_t)(value - min) * INPUT_ABS_MAX / (max - min));
}

bool virtio_pointer_event(struct virtio_pointer_state *state, uint16_t type, uint16_t code,
                          uint32_t value, struct input_event *event) {
    int32_t v = (int32_t)value;
    switch (type) {
    case EV_REL:
        if (code == REL_X) {
            state->dx += v;
        } else if (code == REL_Y) {
            state->dy += v;
        } else if (code == REL_WHEEL) {
            state->wheel += v;
        } else {
            return false;
        }
        state->changed = true;
        return false;
    case EV_ABS:
        if (code == ABS_X) {
            state->x = scale(v, state->min_x, state->max_x);
        } else if (code == ABS_Y) {
            state->y = scale(v, state->min_y, state->max_y);
        } else {
            return false;
        }
        state->changed = true;
        return false;
    case EV_KEY:
        if (code < BTN_LEFT || code > BTN_EXTRA) {
            return false;
        }
        if (value != 0) {
            state->buttons |= (uint8_t)(1u << (code - BTN_LEFT));
        } else {
            state->buttons &= (uint8_t)~(1u << (code - BTN_LEFT));
        }
        state->changed = true;
        return false;
    case EV_SYN:
        if (code != SYN_REPORT || !state->changed) {
            return false;
        }
        *event = (struct input_event){.type = INPUT_EVENT_MOUSE, .buttons = state->buttons};
        if (state->absolute) {
            event->flags = INPUT_FLAG_ABSOLUTE;
            event->x = state->x;
            event->y = state->y;
        } else {
            event->x = clamp16(state->dx);
            event->y = clamp16(state->dy);
        }
        event->wheel = (int8_t)(state->wheel < -128 ? -128 : state->wheel > 127 ? 127 : state->wheel);
        state->dx = state->dy = state->wheel = 0;
        state->changed = false;
        return true;
    default:
        return false;
    }
}
