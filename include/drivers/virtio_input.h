// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Initialize one modern virtio MMIO keyboard discovered by the platform DTB.
// IRQ input feeds the existing TTY; failure leaves the serial input available.
bool virtio_input_init(void);

// Pure US evdev key translation, shared with host tests. A result may contain
// a NUL control byte, so length (rather than string termination) is authoritative.
struct virtio_keymap_state {
    uint8_t shift;
    uint8_t control;
    bool caps_lock;
    bool caps_down;
};
struct virtio_keymap_result {
    char bytes[4];
    unsigned length;
    int scroll;
};
void virtio_keymap_event(struct virtio_keymap_state *state, uint16_t type,
                         uint16_t code, uint32_t value,
                         struct virtio_keymap_result *result);
