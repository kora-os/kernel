// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// keylog: the debug console's trace of the input event queue, in the manner
// of X11's xev. When it is on, every event taken out of the queue is printed
// on the UART with the device that sent it, who took it and, when a screen
// manager took it, the process it says the events go on to:
//
//   KeyDown from usb-keyboard to screens (pid 2) -> front: condemo (pid 7)
//       time 123456 ms, keycode 0x04 (A), modifiers 0x0008 (LGUI)
//       char U+0061 "a", repeat no
//
// Taking an event only notes it (keylog_note, any context, under any lock);
// keylog_flush() prints the notes later, from task context with no spinlock
// held, so the slow UART never runs with interrupts masked for a key.

#ifdef __cplusplus
extern "C" {
#endif

// Notes kept before flushing; more are counted and reported as not shown.
#define KEYLOG_NOTES 64

void keylog_enable(bool on);
bool keylog_enabled(void);

// Who took an event: the tty adapter, on behalf of the thread reading the
// console, or the thread that claimed the queue.
#define KEYLOG_TO_CONSOLE 0
#define KEYLOG_TO_CLAIMER 1

struct keylog_note {
    struct input_event event;
    uint8_t to;     // KEYLOG_TO_*
    int tid;        // the reading or claiming thread
    int focus_pid;  // KEYLOG_TO_CLAIMER: where the claimer sends it, or 0
};

// Note an event that `tid` took (`to`: KEYLOG_TO_*), with the queue's focus
// for a claimer. Does nothing while keylog is off.
void keylog_note(const struct input_event *event, int to, int tid, int focus_pid);

// Print the pending notes, and the events the queue dropped meanwhile. Task
// context, BKL held, no spinlock.
void keylog_flush(void);

// The text of one note, at most KEYLOG_TEXT_MAX bytes with its terminating
// NUL: the lines shown above, each ending in '\n'. `source` is the device's
// name; `receiver` and `focus` name the threads ("screens (pid 2)"), focus
// NULL for none.
#define KEYLOG_TEXT_MAX 384
void keylog_format(char *out, const struct keylog_note *note, const char *source,
                   const char *receiver, const char *focus);

// A key's name ("A", "Enter", "LGUI"), or NULL for a usage without one.
const char *keylog_key_name(uint16_t keycode);

#ifdef __cplusplus
}
#endif
