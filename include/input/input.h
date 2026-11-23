// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"
#include "input/event.h"

// The kernel's input event queue: keyboard drivers post events from their
// interrupt handlers, one consumer takes them out in task context. A fixed
// ring, no allocation; see docs/locking.md for its lock.
//
// The consumer is the kernel's tty adapter (src/tty.c) unless a thread has
// claimed the queue (input_claim): a screen manager, which then takes the
// events with input_take() and is signalled when one arrives.

#ifdef __cplusplus
extern "C" {
#endif

// Events the queue holds before it drops new ones: a key is two events (down
// and up), so about 128 keystrokes typed ahead of a consumer that is busy.
#define INPUT_QUEUE_EVENTS 256  // power of two

// Devices that can be named as an event's source (event.source 1 to this).
#define INPUT_SOURCES 8

// Give a device a source number for its events, named `name` (a string that
// outlives the kernel, such as a literal). Returns 1 to INPUT_SOURCES, or 0
// when the table is full (its events then carry no source). Any context.
unsigned input_source_register(const char *name);

// A source's name, or "unknown" for 0 or a number never handed out.
const char *input_source_name(unsigned source);

// Queue a copy of `event`, stamped with the uptime, and wake the consumer:
// the claiming thread's signal, or input_consumer_wake(). Safe from interrupt
// handlers on any core. A full queue drops the event and counts it: after
// that a consumer may have missed a key up, which is why every event carries
// the whole modifier state. Returns whether the event was queued.
bool input_post(const struct input_event *event);

// The tty adapter's take: the oldest event, unless a thread has claimed the
// queue. Returns false, leaving `event` alone, when the queue is empty or
// claimed. Never blocks; safe from any context.
bool input_poll(struct input_event *event);

// Make thread `tid` the queue's consumer: from now on each queued event sets
// its signal `bit`, and only it takes events (input_take). Events already
// queued stay for it, and it is signalled at once if there are any. Returns 0,
// or -1 if another thread holds the claim. Claiming again changes the bit.
int input_claim(int tid, int bit);

// Thread `tid` gives the queue back, if it holds it; the tty adapter is woken
// to take what is left. Called for a thread that ends, and when it frees its
// claim's signal bit.
void input_release(int tid);
void input_signal_freed(int tid, int bit);

// The claiming thread, or 0.
int input_claimer(void);

// The claimer `tid` takes up to `max` events, oldest first. Returns how many
// (0 when the queue is empty), or -1 if `tid` does not hold the claim.
int input_take(int tid, struct input_event *events, int max);

// The claimer says which process the events go on to (the owner of the front
// screen), or 0 for none: for keylog only, the kernel does nothing with it.
// Returns -1 if `tid` does not hold the claim.
int input_set_focus(int tid, int pid);
int input_focus(void);

// Events dropped on a full queue since boot.
unsigned input_dropped(void);

// Implemented by the tty adapter (src/tty.c): called by input_post() after an
// event was queued while nobody claims the queue, and by input_release(),
// with no input lock held, possibly from an interrupt handler. It wakes
// whoever sleeps until input arrives.
void input_consumer_wake(void);

#ifdef __cplusplus
}
#endif
