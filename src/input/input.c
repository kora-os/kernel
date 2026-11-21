// SPDX-License-Identifier: GPL-3.0-or-later
//
// Input event queue. See input/input.h.

#include "input/input.h"

#include "arch/spinlock.h"
#include "lib/timer.h"
#include "proc/task.h"

// queue_lock (IRQ-safe) covers the ring, the drop counter, the claim and the
// source table. It is a leaf: nothing is called while it is held, the
// consumer's wake-up and the claimer's signal included.
static struct spinlock queue_lock = SPINLOCK_INIT("input events");
static struct input_event queue[INPUT_QUEUE_EVENTS];
static unsigned queue_head;
static unsigned queue_tail;
static unsigned dropped;

// The claiming thread and its signal bit (claim_tid 0: the tty adapter), and
// the process it says the events go on to.
static int claim_tid;
static int claim_bit;
static int focus_pid;

static const char *sources[INPUT_SOURCES];
static unsigned source_count;

unsigned input_source_register(const char *name) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    unsigned source = 0;
    if (source_count < INPUT_SOURCES) {
        sources[source_count++] = name;
        source = source_count;
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    return source;
}

const char *input_source_name(unsigned source) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    const char *name = source >= 1 && source <= source_count ? sources[source - 1] : "unknown";
    spin_unlock_irqrestore(&queue_lock, flags);
    return name;
}

// Wake whoever consumes the queue: the claimer's signal, or the tty adapter.
static void wake_consumer(int tid, int bit) {
    if (tid != 0) {
        task_signal(tid, 1u << bit);
    } else {
        input_consumer_wake();
    }
}

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
    int tid = claim_tid, bit = claim_bit;
    spin_unlock_irqrestore(&queue_lock, flags);

    if (queued) {
        wake_consumer(tid, bit);
    }
}

// Under queue_lock.
static bool take_locked(struct input_event *event) {
    if (queue_tail == queue_head) {
        return false;
    }
    *event = queue[queue_tail % INPUT_QUEUE_EVENTS];
    queue_tail++;
    return true;
}

bool input_poll(struct input_event *event) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool found = claim_tid == 0 && take_locked(event);
    spin_unlock_irqrestore(&queue_lock, flags);
    return found;
}

int input_claim(int tid, int bit) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool free = tid > 0 && (claim_tid == 0 || claim_tid == tid);
    bool pending = queue_tail != queue_head;
    if (free) {
        if (claim_tid != tid) {
            focus_pid = 0;
        }
        claim_tid = tid;
        claim_bit = bit;
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    if (free && pending) {
        wake_consumer(tid, bit);
    }
    return free ? 0 : -1;
}

void input_release(int tid) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool held = tid != 0 && claim_tid == tid;
    if (held) {
        claim_tid = 0;
        focus_pid = 0;
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    if (held) {
        input_consumer_wake();  // the tty adapter takes what was left
    }
}

void input_signal_freed(int tid, int bit) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool tied = tid != 0 && claim_tid == tid && claim_bit == bit;
    spin_unlock_irqrestore(&queue_lock, flags);
    if (tied) {
        input_release(tid);
    }
}

int input_claimer(void) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    int tid = claim_tid;
    spin_unlock_irqrestore(&queue_lock, flags);
    return tid;
}

int input_take(int tid, struct input_event *events, int max) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    int count = -1;
    if (tid != 0 && claim_tid == tid) {
        count = 0;
        while (count < max && take_locked(&events[count])) {
            count++;
        }
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    return count;
}

int input_set_focus(int tid, int pid) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    bool held = tid != 0 && claim_tid == tid;
    if (held) {
        focus_pid = pid;
    }
    spin_unlock_irqrestore(&queue_lock, flags);
    return held ? 0 : -1;
}

int input_focus(void) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    int pid = focus_pid;
    spin_unlock_irqrestore(&queue_lock, flags);
    return pid;
}

unsigned input_dropped(void) {
    uint64_t flags = spin_lock_irqsave(&queue_lock);
    unsigned count = dropped;
    spin_unlock_irqrestore(&queue_lock, flags);
    return count;
}
