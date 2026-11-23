// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the input event queue (src/input/input.c). The spinlock is
// stubbed with a counter, to check that every critical section is balanced
// and that the consumer (the tty adapter, or a claiming thread's signal) is
// woken with the queue's lock released; the clock is a variable.

#include "test.h"
#include "arch/spinlock.h"
#include "input/hid_keys.h"
#include "input/input.h"
#include "lib/timer.h"
#include "proc/task.h"

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

static int signalled_tid;
static uint32_t signalled_mask;
static unsigned signals;
int task_signal(int tid, uint32_t mask) {
    CHECK(held == 0, "the claimer is signalled without the queue lock");
    signalled_tid = tid;
    signalled_mask = mask;
    signals++;
    return 0;
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

// A mouse event carries the modifiers of the last key event: Shift+click.
static void test_mouse_modifiers(void) {
    struct input_event shift = key(INPUT_EVENT_KEY_DOWN, HID_KEY_LEFT_CTRL + 1);
    shift.modifiers = INPUT_MOD_LEFT_SHIFT;
    struct input_event click = {.type = INPUT_EVENT_MOUSE, .modifiers = 0x00FF,
                                .buttons = INPUT_BUTTON_LEFT, .x = -4, .y = 7, .wheel = 1};
    input_post(&shift);
    input_post(&click);
    struct input_event release = key(INPUT_EVENT_KEY_UP, HID_KEY_LEFT_CTRL + 1);
    input_post(&release);
    input_post(&click);
    struct input_event event;
    CHECK(input_poll(&event) && input_poll(&event), "a key, then a click");
    CHECK(event.type == INPUT_EVENT_MOUSE && event.modifiers == INPUT_MOD_LEFT_SHIFT &&
              event.buttons == INPUT_BUTTON_LEFT && event.x == -4 && event.y == 7 && event.wheel == 1,
          "the click carries Shift held, and its own movement, buttons and wheel");
    CHECK(input_poll(&event) && input_poll(&event) && event.modifiers == 0,
          "after Shift's release, none");
    CHECK(!input_poll(&event), "the queue is empty again");
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


static void test_sources(void) {
    CHECK(strcmp(input_source_name(0), "unknown") == 0, "source 0 is unknown");
    CHECK(strcmp(input_source_name(1), "unknown") == 0, "a number never handed out is unknown");
    CHECK(input_source_register("virtio-keyboard") == 1, "the first source is 1");
    CHECK(input_source_register("usb-keyboard") == 2, "the next is 2");
    CHECK(strcmp(input_source_name(1), "virtio-keyboard") == 0 &&
          strcmp(input_source_name(2), "usb-keyboard") == 0, "sources keep their names");
    for (unsigned i = 3; i <= INPUT_SOURCES; i++) {
        CHECK(input_source_register("more") == i, "source %u", i);
    }
    CHECK(input_source_register("too many") == 0, "a full table hands out 0");
    CHECK(strcmp(input_source_name(INPUT_SOURCES + 1), "unknown") == 0, "past the table");
    struct input_event event = {.type = INPUT_EVENT_KEY_DOWN, .keycode = HID_KEY_A, .source = 2};
    input_post(&event);
    CHECK(input_poll(&event) && event.source == 2, "an event keeps its source");
    CHECK(held == 0, "every critical section is balanced");
}

static void test_claim(void) {
    struct input_event event = key(INPUT_EVENT_KEY_DOWN, HID_KEY_A), got;
    CHECK(input_claimer() == 0 && input_focus() == 0, "nobody claims the queue at first");
    CHECK(input_claim(0, 9) == -1, "no thread 0");
    CHECK(input_take(7, &got, 1) == -1, "only a claimer takes");
    CHECK(input_set_focus(7, 3) == -1, "only a claimer sets the focus");

    // A claim with events queued signals at once, and they are its.
    input_post(&event);
    wakes = signals = 0;
    CHECK(input_claim(7, 9) == 0 && input_claimer() == 7, "thread 7 claims the queue");
    CHECK(signals == 1 && signalled_tid == 7 && signalled_mask == 1u << 9,
          "a claim with events waiting signals at once");
    CHECK(!input_poll(&got), "the tty adapter takes nothing while it is claimed");
    CHECK(input_claim(8, 10) == -1 && input_claimer() == 7, "a claim is held by one thread");
    CHECK(input_take(8, &got, 1) == -1, "another thread takes nothing");
    struct input_event many[4];
    CHECK(input_take(7, many, 4) == 1 && many[0].keycode == HID_KEY_A, "the claimer takes it");
    CHECK(input_take(7, many, 4) == 0, "and then finds none");

    // Each event signals the claimer, not the tty adapter.
    wakes = signals = 0;
    for (int i = 0; i < 6; i++) {
        struct input_event more = key(INPUT_EVENT_KEY_DOWN, (uint16_t)(HID_KEY_A + i));
        input_post(&more);
    }
    CHECK(signals == 6 && wakes == 0, "events signal the claimer (%u, %u)", signals, wakes);
    CHECK(input_take(7, many, 4) == 4 && many[0].keycode == HID_KEY_A &&
          many[3].keycode == HID_KEY_A + 3, "it takes up to its maximum, in order");

    // Claiming again changes the bit; focus is kept for the same thread.
    CHECK(input_set_focus(7, 42) == 0 && input_focus() == 42, "the claimer names the focus");
    signals = 0;
    CHECK(input_claim(7, 11) == 0 && signals == 1 && signalled_mask == 1u << 11,
          "claiming again moves the signal (two events wait)");
    CHECK(input_focus() == 42, "and keeps the focus");

    // Release: the tty adapter is woken and takes what was left.
    wakes = 0;
    input_release(8);
    CHECK(input_claimer() == 7 && wakes == 0, "another thread cannot release it");
    input_release(7);
    CHECK(input_claimer() == 0 && input_focus() == 0 && wakes == 1,
          "release wakes the tty adapter and clears the focus");
    CHECK(input_poll(&got) && got.keycode == HID_KEY_A + 4 && input_poll(&got) &&
          got.keycode == HID_KEY_A + 5 && !input_poll(&got), "what was left goes to the tty");
    wakes = signals = 0;
    input_post(&event);
    CHECK(wakes == 1 && signals == 0, "events wake the tty adapter again");
    CHECK(input_poll(&got), "and are its");

    // Freeing the claim's bit gives the queue back; another bit does not.
    CHECK(input_claim(5, 12) == 0, "thread 5 claims");
    input_signal_freed(5, 13);
    CHECK(input_claimer() == 5, "freeing another bit keeps the claim");
    input_signal_freed(6, 12);
    CHECK(input_claimer() == 5, "another thread's bit keeps it");
    input_signal_freed(5, 12);
    CHECK(input_claimer() == 0, "freeing the claim's bit releases it");
    CHECK(input_claim(8, 10) == 0 && input_focus() == 0, "a new claimer starts with no focus");
    input_release(8);
    CHECK(held == 0, "every critical section is balanced");
}

TEST_MAIN(test_empty, test_order_and_time, test_mouse_modifiers, test_overflow_and_wraparound,
          test_sources, test_claim)
