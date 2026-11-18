// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input event queue. See input/input.h.

#include "input/input.h"

#include "arch/spinlock.h"
#include "lib/timer.h"

// queue_lock (IRQ-safe) covers the ring and the drop counter. It is a leaf:
// nothing is called while it is held, the consumer's wake-up included.
static struct spinlock queue_lock = SPINLOCK_INIT("input events");
static struct input_event queue[INPUT_QUEUE_EVENTS];
static unsigned queue_head;
static unsigned queue_tail;
static unsigned dropped;

void input_post(const struct input_event *event) {
    struct input_event stamped = *event;
    stamped.time_ms = (uint32_t)(timer_us() / 1000);

    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool queued = queue_head - queue_tail != INPUT_QUEUE_EVENTS;
    if (queued) {
        queue[queue_head % INPUT_QUEUE_EVENTS] = stamped;
        queue_head++;
    } else {
        dropped++;
    }
    spin_unlock_irqrestore(&queue_lock, flags);

    if (queued) {
        input_consumer_wake();
    }
}

bool input_poll(struct input_event *event) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool found = queue_tail != queue_head;
    if (found) {
        *event = queue[queue_tail % INPUT_QUEUE_EVENTS];
        queue_tail++;
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    return found;
}

unsigned input_dropped(void) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    unsigned count = dropped;
    spin_unlock_irqrestore(&queue_lock, flags);
    return count;
}
