// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// The kernel's input event queue: keyboard drivers post events from their
// interrupt handlers, one consumer takes them out in task context. A fixed
// ring, no allocation; see docs/locking.md for its lock.

#ifdef __cplusplus
extern "C" {
#endif

// Events the queue holds before it drops new ones: a key is two events (down
// and up), so about 128 keystrokes typed ahead of a consumer that is busy.
#define INPUT_QUEUE_EVENTS 256  // power of two

// Queue a copy of `event`, stamped with the uptime, and wake the consumer.
// Safe from interrupt handlers on any core. A full queue drops the event and
// counts it: after that a consumer may have missed a key up, which is why
// every event carries the whole modifier state.
void input_post(const struct input_event *event);

// Take the oldest event. Returns false, leaving `event` alone, when the queue
// is empty. Never blocks; safe from any context.
bool input_poll(struct input_event *event);

// Events dropped on a full queue since boot.
unsigned input_dropped(void);

// Implemented by the queue's consumer (src/tty.c for now): called by
// input_post() after an event was queued, with no input lock held, possibly
// from an interrupt handler. It wakes whoever sleeps until input arrives.
void input_consumer_wake(void);

#ifdef __cplusplus
}
#endif
