// SPDX-License-Identifier: GPL-3.0-or-later
//
// keylog, the debug console's trace of the input event queue. See
// input/keylog.h.

#include "input/keylog.h"

#include "arch/spinlock.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "lib/printf.h"
#include "mini_uart.h"
#include "proc/task.h"

// notes_lock (IRQ-safe) covers the notes and the on switch. A leaf: it is
// taken under the tty's input lock, and nothing is called while it is held.
static struct spinlock notes_lock = SPINLOCK_INIT("keylog");
static bool enabled;
static struct keylog_note notes[KEYLOG_NOTES];
static unsigned notes_head;
static unsigned notes_tail;
static unsigned notes_lost;
static unsigned dropped_seen;  // input_dropped() at the last flush

void keylog_enable(bool on) {
    unsigned dropped = input_dropped();
    uint64_t flags = spin_lock_irqsave(&notes_lock);
    if (on && !enabled) {
        notes_tail = notes_head;
        notes_lost = 0;
        dropped_seen = dropped;
    }
    enabled = on;
    spin_unlock_irqrestore(&notes_lock, flags);
}

bool keylog_enabled(void) {
    uint64_t flags = spin_lock_irqsave(&notes_lock);
    bool on = enabled;
    spin_unlock_irqrestore(&notes_lock, flags);
    return on;
}

void keylog_note(const struct input_event *event, int to, int tid, int focus_pid) {
    uint64_t flags = spin_lock_irqsave(&notes_lock);
    if (enabled) {
        if (notes_head - notes_tail == KEYLOG_NOTES) {
            notes_lost++;
        } else {
            notes[notes_head % KEYLOG_NOTES] = (struct keylog_note){
                .event = *event, .to = (uint8_t)to, .tid = tid, .focus_pid = focus_pid};
            notes_head++;
        }
    }
    spin_unlock_irqrestore(&notes_lock, flags);
}

// --- Text ----------------------------------------------------------------------

static const char *const key_names[] = {
    [0x04] = "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
    "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    [0x1E] = "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    [0x28] = "Enter", "Escape", "Backspace", "Tab", "Space", "Minus", "Equal",
    "LeftBracket", "RightBracket", "Backslash", "NonUSHash", "Semicolon",
    "Apostrophe", "Grave", "Comma", "Period", "Slash", "CapsLock",
    [0x3A] = "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12",
    [0x46] = "PrintScreen", "ScrollLock", "Pause", "Insert", "Home", "PageUp",
    "Delete", "End", "PageDown", "Right", "Left", "Down", "Up", "NumLock",
    [0x54] = "KP/", "KP*", "KP-", "KP+", "KPEnter", "KP1", "KP2", "KP3", "KP4",
    "KP5", "KP6", "KP7", "KP8", "KP9", "KP0", "KP.", "NonUSBackslash",
    "Application", "Power", "KP=",
    [0x68] = "F13", "F14", "F15", "F16", "F17", "F18", "F19", "F20", "F21",
    "F22", "F23", "F24",
    [0xE0] = "LCtrl", "LShift", "LAlt", "LGUI", "RCtrl", "RShift", "RAlt", "RGUI",
};

const char *keylog_key_name(uint16_t keycode) {
    if (keycode >= sizeof(key_names) / sizeof(key_names[0])) {
        return NULL;
    }
    return key_names[keycode];
}

static const char *const modifier_names[] = {
    "LCtrl", "LShift", "LAlt", "LGUI", "RCtrl", "RShift", "RAlt", "RGUI", "Caps", "Num",
};

static char *append(char *out, const char *text) {
    while (*text) {
        *out++ = *text++;
    }
    *out = '\0';
    return out;
}

// The modifier names joined by '+', or "none".
static char *modifiers_text(char *out, uint16_t modifiers) {
    char *start = out;
    for (unsigned i = 0; i < sizeof(modifier_names) / sizeof(modifier_names[0]); i++) {
        if (modifiers & (1u << i)) {
            if (out != start) {
                out = append(out, "+");
            }
            out = append(out, modifier_names[i]);
        }
    }
    return out != start ? out : append(out, "none");
}

// A printable character in UTF-8, between quotes; nothing for a control one.
static char *char_text(char *out, uint32_t c) {
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c < 0xA0) || c > 0x10FFFF ||
        (c >= 0xD800 && c < 0xE000)) {
        return out;
    }
    *out++ = ' ';
    *out++ = '"';
    if (c < 0x80) {
        *out++ = (char)c;
    } else if (c < 0x800) {
        *out++ = (char)(0xC0 | (c >> 6));
        *out++ = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        *out++ = (char)(0xE0 | (c >> 12));
        *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    } else {
        *out++ = (char)(0xF0 | (c >> 18));
        *out++ = (char)(0x80 | ((c >> 12) & 0x3F));
        *out++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *out++ = (char)(0x80 | (c & 0x3F));
    }
    *out++ = '"';
    *out = '\0';
    return out;
}

static char *end(char *out) {
    while (*out) {
        out++;
    }
    return out;
}

// "Left+Middle", or "none".
static char *buttons_text(char *out, uint8_t buttons) {
    static const char *const names[] = {"Left", "Right", "Middle", "Side", "Extra"};
    char *start = out;
    for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (buttons & (1u << i)) {
            out = append(out, out == start ? "" : "+");
            out = append(out, names[i]);
        }
    }
    if (out == start) {
        out = append(out, "none");
    }
    *out = '\0';
    return out;
}

void keylog_format(char *out, const struct keylog_note *note, const char *source,
                   const char *receiver, const char *focus) {
    const struct input_event *e = &note->event;
    char *p = out;
    if (e->type == INPUT_EVENT_MOUSE) {
        p = append(p, "Mouse");
    } else if (e->type == INPUT_EVENT_KEY_DOWN) {
        p = append(p, "KeyDown");
    } else if (e->type == INPUT_EVENT_KEY_UP) {
        p = append(p, "KeyUp");
    } else {
        tfp_sprintf(p, "Event %u", (unsigned)e->type);
        p = end(p);
    }
    p = append(p, " from ");
    p = append(p, source);
    p = append(p, " to ");
    p = append(p, receiver);
    if (focus != NULL) {
        p = append(p, " -> front: ");
        p = append(p, focus);
    }
    if (e->type == INPUT_EVENT_MOUSE) {
        tfp_sprintf(p, (e->flags & INPUT_FLAG_ABSOLUTE) ? "\n    time %u ms, at %d,%d, buttons 0x%02x ("
                                                        : "\n    time %u ms, move %d,%d, buttons 0x%02x (",
                    (unsigned)e->time_ms, (int)e->x, (int)e->y, (unsigned)e->buttons);
        p = buttons_text(end(p), e->buttons);
        tfp_sprintf(p, "), wheel %d, modifiers 0x%04x (", (int)e->wheel, (unsigned)e->modifiers);
        p = modifiers_text(end(p), e->modifiers);
        append(p, ")\n");
        return;
    }
    const char *name = keylog_key_name(e->keycode);
    tfp_sprintf(p, "\n    time %u ms, keycode 0x%02x (%s), modifiers 0x%04x (",
                (unsigned)e->time_ms, (unsigned)e->keycode, name != NULL ? name : "?",
                (unsigned)e->modifiers);
    p = modifiers_text(end(p), e->modifiers);
    p = append(p, ")\n");
    if (e->type == INPUT_EVENT_KEY_DOWN) {
        if (e->codepoint != 0) {
            tfp_sprintf(p, "    char U+%04X", (unsigned)e->codepoint);
            p = char_text(end(p), e->codepoint);
        } else {
            p = append(p, "    char none");
        }
        p = append(p, (e->flags & INPUT_FLAG_REPEAT) ? ", repeat yes\n" : ", repeat no\n");
    }
}

// --- Printing ------------------------------------------------------------------

// "name (pid N)" for thread `tid`'s process, or "pid N" once it is gone.
static void thread_text(char *out, int tid) {
    char name[TASK_NAME_MAX];
    int pid;
    if (task_describe(tid, name, &pid)) {
        tfp_sprintf(out, "%s (pid %d)", name, pid);
    } else {
        tfp_sprintf(out, "pid %d", tid);
    }
}

static void process_text(char *out, int pid) {
    char name[TASK_NAME_MAX];
    if (proc_describe(pid, name)) {
        tfp_sprintf(out, "%s (pid %d)", name, pid);
    } else {
        tfp_sprintf(out, "pid %d", pid);
    }
}

void keylog_flush(void) {
    for (;;) {
        unsigned dropped = input_dropped();
        uint64_t flags = spin_lock_irqsave(&notes_lock);
        struct keylog_note note;
        bool found = notes_tail != notes_head;
        if (found) {
            note = notes[notes_tail % KEYLOG_NOTES];
            notes_tail++;
        }
        unsigned lost = notes_lost;
        notes_lost = 0;
        unsigned new_drops = enabled ? dropped - dropped_seen : 0;
        dropped_seen = dropped;
        spin_unlock_irqrestore(&notes_lock, flags);

        char text[KEYLOG_TEXT_MAX + 64];
        if (new_drops != 0) {
            tfp_sprintf(text, "keylog: the input queue dropped %u events\n", new_drops);
            uart_puts(text);
        }
        if (lost != 0) {
            tfp_sprintf(text, "keylog: %u events not shown\n", lost);
            uart_puts(text);
        }
        if (!found) {
            return;
        }
        char receiver[TASK_NAME_MAX + 48], focus[TASK_NAME_MAX + 24];
        if (note.to == KEYLOG_TO_CONSOLE) {
            char reader[TASK_NAME_MAX + 24];
            thread_text(reader, note.tid);
            tfp_sprintf(receiver, "kernel console (read by %s)", reader);
        } else {
            thread_text(receiver, note.tid);
        }
        if (note.to == KEYLOG_TO_CLAIMER && note.focus_pid != 0) {
            process_text(focus, note.focus_pid);
        }
        keylog_format(text, &note, input_source_name(note.event.source), receiver,
                      note.to == KEYLOG_TO_CLAIMER && note.focus_pid != 0 ? focus : NULL);
        uart_puts(text);
    }
}
