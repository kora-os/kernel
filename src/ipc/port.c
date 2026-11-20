// SPDX-License-Identifier: GPL-3.0-or-later
//
// Message ports. See ipc/port.h.

#include "ipc/port.h"

#include "arch/spinlock.h"
#include "proc/task.h"

// A port id is its slot (1 .. MAX_PORTS) in the low bits and the slot's
// generation above: a deleted port's id never names its successor.
#define SLOT_BITS 6
#define SLOT_MASK ((1 << SLOT_BITS) - 1)
#define GEN_MASK 0xFFFFFF  // keeps ids positive
_Static_assert(MAX_PORTS <= SLOT_MASK, "port slots fit the id");

typedef enum {
    MSG_FREE = 0,
    MSG_QUEUED,  // on a port's queue (`on`)
    MSG_HELD,    // taken by a receiver (`holder`), not replied yet
} msg_state_t;

// The kernel's record of a message in flight. The message itself is the
// sender's memory, which the kernel never touches.
struct port_msg {
    msg_state_t state;
    int kind;                // PORT_MESSAGE, PORT_REPLY or PORT_RETURNED
    uint64_t msg;
    struct process *sender;
    int sender_pid;
    int reply;               // the sender's port it goes back to, or 0
    struct port *on;         // MSG_QUEUED
    struct process *holder;  // MSG_HELD
    struct port_msg *next;   // next on the same queue
};

struct port {
    bool used;
    uint32_t gen;
    struct process *owner;
    char name[PORT_NAME_MAX];  // "" for an anonymous port
    struct port_msg *head, *tail;
    struct wait_queue waiters;  // the owner's threads in port_wait
    int sig_tid;                // the owner's thread to signal on arrival (0: none)
    int sig_bit;
};

static struct port ports[MAX_PORTS];
static struct port_msg messages[MAX_PORT_MESSAGES];

// port_lock only hands a waiter over to the scheduler: the state above is
// BKL-protected, and wait_queue_sleep needs a spinlock for its condition.
static struct spinlock port_lock = SPINLOCK_INIT("ports");

static int port_id(const struct port *p) {
    return (int)(((p->gen & GEN_MASK) << SLOT_BITS) | (uint32_t)(p - ports + 1));
}

static struct port *lookup(int id) {
    int slot = (id & SLOT_MASK) - 1;
    if (id <= 0 || slot < 0 || slot >= MAX_PORTS) {
        return NULL;
    }
    struct port *p = &ports[slot];
    if (!p->used || port_id(p) != id) {
        return NULL;
    }
    return p;
}

static void wake(struct port *p) {
    uint64_t flags = spin_lock_irqsave(&port_lock);
    wait_queue_wake_all(&p->waiters);
    spin_unlock_irqrestore(&port_lock, flags);
}

static void enqueue(struct port *p, struct port_msg *m) {
    m->state = MSG_QUEUED;
    m->on = p;
    m->holder = NULL;
    m->next = NULL;
    if (p->tail) {
        p->tail->next = m;
    } else {
        p->head = m;
    }
    p->tail = m;
    wake(p);
    if (p->sig_tid != 0) {
        task_signal(p->sig_tid, 1u << p->sig_bit);
    }
}

// Take `m` off the queue it is on.
static void unlink(struct port_msg *m) {
    struct port *p = m->on;
    struct port_msg *prev = NULL;
    for (struct port_msg *it = p->head; it; prev = it, it = it->next) {
        if (it == m) {
            if (prev) {
                prev->next = m->next;
            } else {
                p->head = m->next;
            }
            if (p->tail == m) {
                p->tail = prev;
            }
            break;
        }
    }
    m->on = NULL;
    m->next = NULL;
}

static void release(struct port_msg *m) {
    *m = (struct port_msg){0};
}

// A receiver let go of a message: one fewer of its sender's messages is held,
// and a sender that ended may now be freed.
static void sender_got_back(struct process *sender) {
    sender->msgs_out--;
    if (sender->msgs_out == 0 && sender->draining) {
        proc_messages_back(sender);
    }
}

// Send `m` back to its sender's reply port as `kind`, or release it if there
// is no such port any more.
static void send_back(struct port_msg *m, int kind) {
    struct port *rp = lookup(m->reply);
    if (rp) {
        m->kind = kind;
        enqueue(rp, m);
    } else {
        release(m);
    }
}

static bool name_equal(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int port_create(struct process *me, const char *name) {
    size_t length = 0;
    if (name) {
        while (name[length]) {
            if (++length >= PORT_NAME_MAX) {
                return -1;
            }
        }
        if (length > 0 && port_find(name) > 0) {
            return -1;
        }
    }
    for (int i = 0; i < MAX_PORTS; i++) {
        struct port *p = &ports[i];
        if (!p->used) {
            p->used = true;
            p->owner = me;
            for (size_t c = 0; c < PORT_NAME_MAX; c++) {
                p->name[c] = c < length ? name[c] : '\0';
            }
            p->head = p->tail = NULL;
            p->sig_tid = 0;
            return port_id(p);
        }
    }
    return -1;
}

// Return the messages queued on `p` to their senders and free the slot.
static void destroy(struct port *p) {
    struct port_msg *m = p->head;
    p->head = p->tail = NULL;
    while (m) {
        struct port_msg *next = m->next;
        m->on = NULL;
        m->next = NULL;
        if (m->kind == PORT_MESSAGE) {
            send_back(m, PORT_RETURNED);
        } else {
            release(m);  // a reply or return for the owner, who is done
        }
        m = next;
    }
    p->used = false;
    p->owner = NULL;
    p->sig_tid = 0;
    p->name[0] = '\0';
    p->gen = (p->gen + 1) & GEN_MASK;
    wake(p);  // waiters find the port gone
}

int port_delete(struct process *me, int id) {
    struct port *p = lookup(id);
    if (!p || p->owner != me) {
        return -1;
    }
    destroy(p);
    return 0;
}

bool port_exists(int id) {
    return lookup(id) != NULL;
}

int port_find(const char *name) {
    if (!name || !name[0]) {
        return -1;
    }
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ports[i].used && name_equal(ports[i].name, name)) {
            return port_id(&ports[i]);
        }
    }
    return -1;
}

int port_put(struct process *me, int id, uint64_t msg, int reply) {
    struct port *dest = lookup(id);
    if (!dest || msg == 0) {
        return -1;
    }
    if (reply != 0) {
        struct port *rp = lookup(reply);
        if (!rp || rp->owner != me) {
            return -1;  // replies come back to the sender's own port
        }
    }
    struct port_msg *slot = NULL;
    for (int i = 0; i < MAX_PORT_MESSAGES; i++) {
        struct port_msg *m = &messages[i];
        if (m->state == MSG_FREE) {
            if (!slot) {
                slot = m;
            }
        } else if (m->sender == me && m->msg == msg) {
            return -1;  // still in flight
        }
    }
    if (!slot) {
        return -1;
    }
    slot->kind = PORT_MESSAGE;
    slot->msg = msg;
    slot->sender = me;
    slot->sender_pid = me->pid;
    slot->reply = reply;
    enqueue(dest, slot);
    return 0;
}

int port_get(struct process *me, int id, struct port_received *out) {
    struct port *p = lookup(id);
    if (!p || p->owner != me) {
        return -1;
    }
    struct port_msg *m = p->head;
    if (!m) {
        return 0;
    }
    unlink(m);
    out->msg = m->msg;
    out->kind = m->kind;
    out->sender = m->sender_pid;
    if (m->kind == PORT_MESSAGE) {
        m->state = MSG_HELD;
        m->holder = me;
        m->sender->msgs_out++;
    } else {
        release(m);  // back with its owner
    }
    return 1;
}

int port_wait(struct process *me, int id, struct port_received *out) {
    for (;;) {
        int got = port_get(me, id, out);
        if (got != 0) {
            return got;
        }
        if (task_killed()) {
            return -1;
        }
        // Under the BKL nothing can be queued between the check above and
        // the sleep: a sender needs the BKL, which the sleep gives up.
        struct port *p = lookup(id);
        uint64_t flags = spin_lock_irqsave(&port_lock);
        wait_queue_sleep(&p->waiters, &port_lock);
        spin_unlock_irqrestore(&port_lock, flags);
    }
}

int port_set_signal(struct process *me, int id, int tid, int bit) {
    struct port *p = lookup(id);
    if (!p || p->owner != me) {
        return -1;
    }
    p->sig_tid = bit >= 0 ? tid : 0;
    p->sig_bit = bit;
    if (p->sig_tid != 0 && p->head) {
        task_signal(p->sig_tid, 1u << bit);  // nothing queued may go unnoticed
    }
    return 0;
}

void port_signal_freed(int tid, int bit) {
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ports[i].used && ports[i].sig_tid == tid && ports[i].sig_bit == bit) {
            ports[i].sig_tid = 0;
        }
    }
}

int port_reply(struct process *me, uint64_t msg) {
    for (int i = 0; i < MAX_PORT_MESSAGES; i++) {
        struct port_msg *m = &messages[i];
        if (m->state == MSG_HELD && m->holder == me && m->msg == msg) {
            struct process *sender = m->sender;
            send_back(m, PORT_REPLY);
            sender_got_back(sender);
            return 0;
        }
    }
    return -1;
}

void port_process_exit(struct process *p) {
    for (int i = 0; i < MAX_PORT_MESSAGES; i++) {
        struct port_msg *m = &messages[i];
        if (m->state == MSG_HELD && m->holder == p) {
            // A receiver that ended without replying: back unhandled.
            struct process *sender = m->sender;
            send_back(m, PORT_RETURNED);
            sender_got_back(sender);
        } else if (m->state == MSG_QUEUED && m->sender == p && m->kind == PORT_MESSAGE) {
            unlink(m);  // nobody took it yet: withdraw it
            release(m);
        }
    }
    for (int i = 0; i < MAX_PORTS; i++) {
        if (ports[i].used && ports[i].owner == p) {
            destroy(&ports[i]);
        }
    }
}

unsigned port_messages_out(const struct process *p) {
    return p->msgs_out;
}
