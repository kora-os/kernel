// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the input event queue (src/input/input.c). The spinlock is
// stubbed with a counter, to check that every critical section is balanced
// and that the consumer is woken with the queue's lock released; the clock is
// a variable.

#include "test.h"
#include "arch/spinlock.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "lib/timer.h"

static int held;
static uint64_t now_us;
static unsigned wakes;

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    (void)lock;
    CHECK(held == 0, "the queue lock is not taken twice");
    held++;
    return 0;
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    (void)lock;
    (void)flags;
    held--;
}
uint64_t timer_us(void) { return now_us; }
void input_consumer_wake(void) {
    CHECK(held == 0, "the consumer is woken without the queue lock");
    wakes++;
}

static struct input_event key(uint8_t type, uint16_t keycode) {
    return (struct input_event){.type = type, .keycode = keycode};
}

static void test_empty(void) {
    struct input_event event = key(INPUT_EVENT_KEY_UP, 0x1234);
    CHECK(!input_poll(&event), "an empty queue has no event");
    CHECK(event.type == INPUT_EVENT_KEY_UP && event.keycode == 0x1234,
          "a failed poll leaves the caller's event alone");
    CHECK(held == 0, "poll releases the lock");
}

static void test_order_and_time(void) {
    struct input_event down = {
        .type = INPUT_EVENT_KEY_DOWN, .flags = INPUT_FLAG_REPEAT,
        .modifiers = INPUT_MOD_LEFT_SHIFT | INPUT_MOD_CAPS_LOCK,
        .time_ms = 99, .keycode = HID_KEY_A, .codepoint = 'A'
    };
    struct input_event up = key(INPUT_EVENT_KEY_UP, HID_KEY_A);
    wakes = 0;
    now_us = 1234567;
    input_post(&down);
    now_us = 1999999;
    input_post(&up);
    CHECK(wakes == 2, "each queued event wakes the consumer (%u)", wakes);
    CHECK(down.time_ms == 99, "the caller's event is not written to");

    struct input_event event;
    CHECK(input_poll(&event), "first event is there");
    CHECK(event.type == INPUT_EVENT_KEY_DOWN && event.flags == INPUT_FLAG_REPEAT &&
          event.modifiers == (INPUT_MOD_LEFT_SHIFT | INPUT_MOD_CAPS_LOCK) &&
          event.keycode == HID_KEY_A && event.codepoint == 'A',
          "an event comes out as it went in");
    CHECK(event.time_ms == 1234, "the queue stamps the uptime in ms (%u)", event.time_ms);
    CHECK(input_poll(&event), "second event is there");
    CHECK(event.type == INPUT_EVENT_KEY_UP && event.time_ms == 1999, "events keep their order");
    CHECK(!input_poll(&event), "the queue is empty again");
    CHECK(held == 0, "post and poll release the lock");
}

// Fill past the end, more than once, from different ring positions: a full
// queue drops the newest events and counts them, and keeps what it holds.
static void test_overflow_and_wraparound(void) {
    unsigned keycode = 0;
    for (unsigned round = 0; round < 5; round++) {
        unsigned dropped_before = input_dropped();
        unsigned first = keycode;
        wakes = 0;
        for (unsigned i = 0; i < INPUT_QUEUE_EVENTS + 7; i++) {
            struct input_event event = key(INPUT_EVENT_KEY_DOWN, (uint16_t)keycode++);
            input_post(&event);
        }
        CHECK(wakes == INPUT_QUEUE_EVENTS, "a dropped event wakes nobody (%u)", wakes);
        CHECK(input_dropped() - dropped_before == 7, "drops are counted (%u)",
              input_dropped() - dropped_before);
        // Take some, add some: the ring's start moves by a different amount
        // each round.
        unsigned taken = 3 + round * 41;
        struct input_event event;
        for (unsigned i = 0; i < taken; i++) {
            CHECK(input_poll(&event) && event.keycode == (uint16_t)(first + i),
                  "oldest events come first (round %u, event %u)", round, i);
        }
        unsigned refill = keycode;
        for (unsigned i = 0; i < taken; i++) {
            struct input_event more = key(INPUT_EVENT_KEY_DOWN, (uint16_t)keycode++);
            input_post(&more);
        }
        CHECK(input_dropped() - dropped_before == 7, "freed slots are used again");
        for (unsigned i = taken; i < INPUT_QUEUE_EVENTS; i++) {
            CHECK(input_poll(&event) && event.keycode == (uint16_t)(first + i),
                  "events before the overflow survive it (round %u, event %u)", round, i);
        }
        for (unsigned i = 0; i < taken; i++) {
            CHECK(input_poll(&event) && event.keycode == (uint16_t)(refill + i),
                  "events after the overflow follow (round %u, event %u)", round, i);
        }
        CHECK(!input_poll(&event), "nothing dropped comes back (round %u)", round);
    }
    CHECK(held == 0, "every critical section is balanced");
}

TEST_MAIN(test_empty, test_order_and_time, test_overflow_and_wraparound)
