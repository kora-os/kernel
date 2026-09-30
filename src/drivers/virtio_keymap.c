// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_input.h"

// Linux evdev codes; letter positions use the US physical keyboard layout.
static const char unshifted[128] = {
    [1] = 0x1B, [2] = '1', [3] = '2', [4] = '3', [5] = '4', [6] = '5',
    [7] = '6', [8] = '7', [9] = '8', [10] = '9', [11] = '0', [12] = '-',
    [13] = '=', [14] = 0x7F, [15] = '\t', [16] = 'q', [17] = 'w', [18] = 'e',
    [19] = 'r', [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o',
    [25] = 'p', [26] = '[', [27] = ']', [28] = '\n', [30] = 'a', [31] = 's',
    [32] = 'd', [33] = 'f', [34] = 'g', [35] = 'h', [36] = 'j', [37] = 'k',
    [38] = 'l', [39] = ';', [40] = '\'', [41] = '`', [43] = '\\', [44] = 'z',
    [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b', [49] = 'n', [50] = 'm',
    [51] = ',', [52] = '.', [53] = '/', [57] = ' ', [96] = '\n', [98] = '/'
};
static const char shifted[128] = {
    [2] = '!', [3] = '@', [4] = '#', [5] = '$', [6] = '%', [7] = '^',
    [8] = '&', [9] = '*', [10] = '(', [11] = ')', [12] = '_', [13] = '+',
    [26] = '{', [27] = '}', [39] = ':', [40] = '"', [41] = '~', [43] = '|',
    [51] = '<', [52] = '>', [53] = '?'
};

static void sequence(struct virtio_keymap_result *result, const char *bytes) {
    while (*bytes && result->length < sizeof(result->bytes)) {
        result->bytes[result->length++] = *bytes++;
    }
}

void virtio_keymap_event(struct virtio_keymap_state *state, uint16_t type,
                         uint16_t code, uint32_t value,
                         struct virtio_keymap_result *result) {
    if (!result) {
        return;
    }
    *result = (struct virtio_keymap_result){0};
    if (!state) {
        return;
    }
    if (type == 0 && code == 3) { // EV_SYN/SYN_DROPPED: avoid stuck modifiers
        *state = (struct virtio_keymap_state){0};
        return;
    }
    if (type != 1 || value > 2) { // EV_KEY only; release, press, repeat
        return;
    }
    uint8_t mask;
    if (code == 42 || code == 54) {
        mask = code == 42 ? 1 : 2;
        state->shift = value ? state->shift | mask : state->shift & ~mask;
        return;
    }
    if (code == 29 || code == 97) {
        mask = code == 29 ? 1 : 2;
        state->control = value ? state->control | mask : state->control & ~mask;
        return;
    }
    if (code == 58) {
        if (value == 1 && !state->caps_down) {
            state->caps_lock = !state->caps_lock;
        }
        state->caps_down = value != 0;
        return;
    }
    if (!value) {
        return;
    }
    if (state->shift && (code == 104 || code == 109)) {
        result->scroll = code == 104 ? 1 : -1;
        return;
    }
    switch (code) {
    case 103: sequence(result, "\x1B[A"); return;
    case 108: sequence(result, "\x1B[B"); return;
    case 106: sequence(result, "\x1B[C"); return;
    case 105: sequence(result, "\x1B[D"); return;
    case 102: sequence(result, "\x1B[H"); return;
    case 107: sequence(result, "\x1B[F"); return;
    case 104: sequence(result, "\x1B[5~"); return;
    case 109: sequence(result, "\x1B[6~"); return;
    case 110: sequence(result, "\x1B[2~"); return;
    case 111: sequence(result, "\x1B[3~"); return;
    default: break;
    }
    if (code >= sizeof(unshifted)) {
        return;
    }
    char c = unshifted[code];
    if (!c) {
        return;
    }
    if (c >= 'a' && c <= 'z') {
        if (state->control) {
            c = c - 'a' + 1;
        } else if ((state->shift != 0) != (state->caps_lock != 0)) {
            c -= 'a' - 'A';
        }
    } else {
        if (state->shift && shifted[code]) {
            c = shifted[code];
        }
        if (state->control) {
            switch (c) {
            case ' ': case '2': case '@': c = 0; break;
            case '[': case '{': c = 0x1B; break;
            case '\\': case '|': c = 0x1C; break;
            case ']': case '}': c = 0x1D; break;
            case '6': case '^': c = 0x1E; break;
            case '-': case '_': case '/': c = 0x1F; break;
            case '?': c = 0x7F; break;
            default: break;
            }
        }
    }
    result->bytes[0] = c;
    result->length = 1;
}
