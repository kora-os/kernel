// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for message ports (src/ipc/port.c). Processes are plain structs;
// the scheduler is stubbed: a sleep in port_wait runs whatever the test
// scheduled "meanwhile" (another process putting a message, deleting the port,
// a kill), and the spinlock is a counter that must balance and never nest.

#include "test.h"
#include "arch/spinlock.h"
#include "ipc/port.h"
#include "proc/task.h"

// --- The scheduler, stubbed ---------------------------------------------------

static int lock_depth;
static unsigned sleeps, wakes;
static bool killed;
static void (*meanwhile)(void);
static struct process *freed[8];
static unsigned nfreed;

uint64_t spin_lock_irqsave(struct spinlock *lock) {
    (void)lock;
    CHECK(lock_depth == 0, "the port lock is never taken twice");
    lock_depth++;
    return 0;
}
void spin_unlock_irqrestore(struct spinlock *lock, uint64_t flags) {
    (void)lock;
    (void)flags;
    lock_depth--;
}
bool task_killed(void) { return killed; }
void wait_queue_sleep(struct wait_queue *wq, struct spinlock *cond) {
    (void)wq;
    (void)cond;
    CHECK(lock_depth == 1, "a waiter sleeps holding the port lock");
    sleeps++;
    void (*action)(void) = meanwhile;
    meanwhile = NULL;
    lock_depth--;  // the scheduler releases it while the task sleeps
    if (action) {
        action();
    } else {
        killed = true;  // nothing will ever come: end the wait
    }
    lock_depth++;
}
void wait_queue_wake_all(struct wait_queue *wq) {
    (void)wq;
    CHECK(lock_depth == 1, "wake-ups happen under the port lock");
    wakes++;
}
// Signals set: the last thread and mask, and how many.
static int signal_tid;
static uint32_t signal_mask;
static unsigned signals;
int task_signal(int tid, uint32_t mask) {
    CHECK(lock_depth == 0, "a port signals outside the port lock");
    signal_tid = tid;
    signal_mask = mask;
    signals++;
    return 0;
}
void proc_messages_back(struct process *p) {
    CHECK(p->draining && p->msgs_out == 0, "a process comes back only when drained");
    if (nfreed < 8) {
        freed[nfreed++] = p;
    }
}

// --- Processes and messages ----------------------------------------------------

static struct process a, b, c;
static uint64_t msg[MAX_PORT_MESSAGES + 4];  // message memory: only addresses are used
#define M(i) ((uint64_t)(uintptr_t)&msg[i])

// Start from scratch: every port deleted through its owner's exit.
static void reset(void) {
    port_process_exit(&a);
    port_process_exit(&b);
    port_process_exit(&c);
    a = (struct process){.pid = 10};
    b = (struct process){.pid = 20};
    c = (struct process){.pid = 30};
    killed = false;
    meanwhile = NULL;
    nfreed = 0;
    sleeps = wakes = 0;
    signals = 0;
    signal_tid = 0;
    signal_mask = 0;
}

static struct port_received got;

static bool received(struct process *me, int port, uint64_t m, int kind, int sender) {
    got = (struct port_received){0};
    return port_get(me, port, &got) == 1 && got.msg == m && got.kind == kind && got.sender == sender;
}

static void test_names_and_ids(void) {
    reset();
    int anon = port_create(&a, NULL);
    int empty = port_create(&a, "");
    int named = port_create(&b, "CON");
    CHECK(anon > 0 && empty > 0 && named > 0 && anon != empty, "ports get positive, distinct ids");
    CHECK(port_find("CON") == named, "a named port is found by name");
    CHECK(port_exists(anon) && port_exists(named) && !port_exists(0) && !port_exists(named + 64),
          "port_exists knows live ids only");
    CHECK(port_find("con") == -1 && port_find("") == -1 && port_find(NULL) == -1,
          "names match exactly, and the empty name finds nothing");
    CHECK(port_create(&c, "CON") == -1, "a name is unique");
    CHECK(port_create(&c, "fifteen-chars-x") > 0 && port_create(&c, "sixteen-chars-xx") == -1,
          "a name is at most %d bytes", PORT_NAME_MAX - 1);

    CHECK(port_delete(&a, named) == -1, "only the owner deletes a port");
    CHECK(port_delete(&b, named) == 0 && port_find("CON") == -1, "a deleted port loses its name");
    int again = port_create(&c, "CON");
    CHECK(again > 0 && again != named, "a new port in the old slot gets a new id");
    CHECK(port_put(&a, named, M(0), 0) == -1 && port_delete(&b, named) == -1 &&
              port_get(&b, named, &got) == -1, "the old id names nothing");
    CHECK(port_put(&a, 0, M(0), 0) == -1 && port_put(&a, -5, M(0), 0) == -1 &&
              port_put(&a, 63, M(0), 0) == -1, "ids that were never handed out fail");

    reset();
    int n = 0;
    while (port_create(&a, NULL) > 0) {
        n++;
    }
    CHECK(n == MAX_PORTS, "%d ports at most (%d)", MAX_PORTS, n);
    port_process_exit(&a);
    CHECK(port_create(&a, NULL) > 0, "an ended process gives its ports back");
}

static void test_put_get_reply(void) {
    reset();
    int server = port_create(&a, "srv");
    int replies = port_create(&b, NULL);
    CHECK(port_get(&a, server, &got) == 0, "an empty port has nothing");
    CHECK(port_get(&b, server, &got) == -1, "only the owner takes messages");
    CHECK(port_put(&b, server, 0, replies) == -1, "message 0 is refused");
    CHECK(port_put(&b, server, M(0), server) == -1, "a reply port must be the sender's own");
    CHECK(port_put(&b, server, M(0), replies) == 0 && port_put(&b, server, M(1), replies) == 0 &&
              port_put(&c, server, M(2), 0) == 0, "any process may put on any port");
    CHECK(port_put(&b, server, M(0), replies) == -1, "a message is in flight once at a time");
    CHECK(port_put(&c, server, M(0), 0) == 0, "the same address from another sender is another message");

    CHECK(received(&a, server, M(0), PORT_MESSAGE, 20) && received(&a, server, M(1), PORT_MESSAGE, 20) &&
              received(&a, server, M(2), PORT_MESSAGE, 30) && received(&a, server, M(0), PORT_MESSAGE, 30),
          "messages come out in order, with their senders");
    CHECK(b.msgs_out == 2 && c.msgs_out == 2 && port_messages_out(&b) == 2,
          "a taken message counts against its sender");
    CHECK(port_reply(&b, M(1)) == -1, "only its receiver replies to a message");
    CHECK(port_reply(&a, M(3)) == -1, "nor to one it does not hold");

    CHECK(port_reply(&a, M(1)) == 0 && b.msgs_out == 1, "a reply gives the message back");
    CHECK(received(&b, replies, M(1), PORT_REPLY, 20), "it arrives on the reply port as a reply");
    CHECK(port_reply(&a, M(1)) == -1, "a message is replied once");
    CHECK(port_put(&b, server, M(1), replies) == 0, "back home, it may be sent again");
    CHECK(port_reply(&a, M(2)) == 0 && c.msgs_out == 1 && port_get(&c, server, &got) == -1,
          "with no reply port a reply only releases the message");
    CHECK(port_reply(&a, M(0)) == 0 && port_reply(&a, M(0)) == 0 && b.msgs_out == 0 && c.msgs_out == 0,
          "the same address from two senders is told apart");
    CHECK(received(&b, replies, M(0), PORT_REPLY, 20) && port_get(&b, replies, &got) == 0,
          "each reply goes to its own sender");
}

static void test_capacity(void) {
    reset();
    int server = port_create(&a, NULL);
    int sent = 0;
    while (sent < MAX_PORT_MESSAGES + 4 && port_put(&b, server, M(sent), 0) == 0) {
        sent++;
    }
    CHECK(sent == MAX_PORT_MESSAGES, "%d messages in flight at most (%d)", MAX_PORT_MESSAGES, sent);
    CHECK(received(&a, server, M(0), PORT_MESSAGE, 20) && port_put(&b, server, M(sent), 0) == -1,
          "a held message is still in flight");
    CHECK(port_reply(&a, M(0)) == 0 && port_put(&b, server, M(sent), 0) == 0,
          "a released one makes room");
}

static bool signalled(int tid, int bit, unsigned count) {
    return signals == count && signal_tid == tid && signal_mask == 1u << bit;
}

static void test_signals(void) {
    reset();
    int server = port_create(&a, NULL);
    int replies = port_create(&b, NULL);
    CHECK(port_put(&b, server, M(0), replies) == 0 && signals == 0, "a port signals nobody by default");
    CHECK(port_set_signal(&b, server, 11, 20) == -1, "only the owner ties a port to a signal");
    CHECK(port_set_signal(&a, server, 11, 20) == 0 && signalled(11, 20, 1),
          "a port with a message waiting signals as soon as it is tied");
    CHECK(port_set_signal(&a, replies, 11, 20) == -1, "nor another's port");
    CHECK(port_put(&c, server, M(1), 0) == 0 && signalled(11, 20, 2), "each arrival signals");
    CHECK(received(&a, server, M(0), PORT_MESSAGE, 20) && received(&a, server, M(1), PORT_MESSAGE, 30) &&
              signals == 2, "taking messages signals nothing");

    CHECK(port_set_signal(&b, replies, 21, 9) == 0 && signals == 2, "an empty port signals when tied, not before");
    CHECK(port_reply(&a, M(0)) == 0 && signalled(21, 9, 3), "a reply arriving signals its port's thread");
    CHECK(received(&b, replies, M(0), PORT_REPLY, 20), "and is there to take");

    CHECK(port_set_signal(&a, server, 12, 25) == 0 && port_put(&b, server, M(2), 0) == 0 &&
              signalled(12, 25, 4), "tying it again replaces the signal");
    CHECK(received(&a, server, M(2), PORT_MESSAGE, 20) && port_reply(&a, M(2)) == 0, "taken");
    CHECK(port_set_signal(&a, server, 12, -1) == 0 && port_put(&b, server, M(3), replies) == 0 && signals == 4,
          "bit -1 unties it");

    CHECK(port_set_signal(&a, server, 12, 25) == 0 && signals == 5, "tied again, with M(3) waiting");
    port_signal_freed(12, 24);
    CHECK(port_put(&c, server, M(4), 0) == 0 && signals == 6, "freeing another bit leaves it tied");
    port_signal_freed(13, 25);
    CHECK(port_put(&c, server, M(5), 0) == 0 && signals == 7, "so does another thread's bit");
    port_signal_freed(12, 25);
    CHECK(port_put(&c, server, M(6), 0) == 0 && signals == 7, "freeing its bit unties it");

    CHECK(port_set_signal(&a, server, 12, 25) == 0 && signals == 8, "tied once more");
    CHECK(port_delete(&a, server) == 0 && signalled(21, 9, 9),
          "a message returned by a deleted port signals its sender's reply port");
    CHECK(received(&b, replies, M(3), PORT_RETURNED, 20), "and is there to take");
    int fresh = port_create(&a, NULL);
    CHECK(port_put(&b, fresh, M(7), 0) == 0 && signals == 9, "a deleted port's signal does not stay with its slot");
}

// What the stubbed sleep runs while a waiter sleeps.
static int wait_port;
static void put_one(void) { CHECK(port_put(&b, wait_port, M(7), 0) == 0, "put while waiting"); }
static void delete_it(void) { CHECK(port_delete(&a, wait_port) == 0, "delete while waiting"); }
static void kill_it(void) { killed = true; }

static void test_wait(void) {
    reset();
    wait_port = port_create(&a, NULL);
    CHECK(port_put(&b, wait_port, M(1), 0) == 0 && port_wait(&a, wait_port, &got) == 1 &&
              got.msg == M(1) && sleeps == 0, "a queued message is taken without sleeping");
    unsigned before = wakes;
    meanwhile = put_one;
    CHECK(port_wait(&a, wait_port, &got) == 1 && got.msg == M(7) && sleeps == 1,
          "an empty port sleeps until a message comes");
    CHECK(wakes == before + 1, "a put wakes the port's waiters");
    meanwhile = kill_it;
    CHECK(port_wait(&a, wait_port, &got) == -1, "a killed waiter gives up");
    killed = false;
    meanwhile = delete_it;
    CHECK(port_wait(&a, wait_port, &got) == -1, "a waiter whose port is deleted gives up");
    CHECK(port_wait(&b, wait_port, &got) == -1 && port_wait(&a, 12345, &got) == -1,
          "nobody waits on a port that is not theirs");
    CHECK(lock_depth == 0, "the port lock is released");
}

static void test_delete_returns(void) {
    reset();
    int server = port_create(&a, NULL);
    int b_replies = port_create(&b, NULL);
    int c_replies = port_create(&c, NULL);
    port_put(&b, server, M(1), b_replies);
    port_put(&c, server, M(2), c_replies);
    port_put(&c, server, M(3), 0);
    CHECK(port_delete(&a, server) == 0, "delete a port with messages on it");
    CHECK(received(&b, b_replies, M(1), PORT_RETURNED, 20) && received(&c, c_replies, M(2), PORT_RETURNED, 30),
          "its messages go back to their senders, unhandled");
    CHECK(port_put(&c, c_replies, M(3), 0) == 0, "one without a reply port is simply released");

    // Replies waiting on a deleted port are dropped with it.
    int server2 = port_create(&a, NULL);
    port_put(&b, server2, M(4), b_replies);
    received(&a, server2, M(4), PORT_MESSAGE, 20);
    port_reply(&a, M(4));
    CHECK(port_delete(&b, b_replies) == 0 && port_put(&b, server2, M(4), 0) == 0,
          "a reply on a deleted port is released");
}

static void test_process_exit(void) {
    // A receiver that ends holding messages: they go back, unhandled.
    reset();
    int server = port_create(&a, "srv");
    int replies = port_create(&b, NULL);
    port_put(&b, server, M(1), replies);
    port_put(&b, server, M(2), replies);
    received(&a, server, M(1), PORT_MESSAGE, 20);
    CHECK(b.msgs_out == 1, "one held, one queued");
    port_process_exit(&a);
    CHECK(port_find("srv") == -1 && port_get(&a, server, &got) == -1, "an ended process's ports are gone");
    CHECK(b.msgs_out == 0, "nothing of b's is held any more");
    CHECK(received(&b, replies, M(1), PORT_RETURNED, 20) && received(&b, replies, M(2), PORT_RETURNED, 20) &&
              port_get(&b, replies, &got) == 0, "the held one and the queued one both come back");

    // A sender that ends: its queued messages are withdrawn...
    reset();
    server = port_create(&a, NULL);
    replies = port_create(&b, NULL);
    port_put(&b, server, M(1), replies);
    port_put(&b, server, M(2), replies);
    port_put(&c, server, M(3), 0);
    received(&a, server, M(1), PORT_MESSAGE, 20);
    port_process_exit(&b);
    CHECK(received(&a, server, M(3), PORT_MESSAGE, 30) && port_get(&a, server, &got) == 0,
          "an ended sender's queued messages are withdrawn");
    // ...but one a receiver holds keeps it alive until it is replied.
    CHECK(port_messages_out(&b) == 1, "the held one still counts");
    b.draining = true;  // what the scheduler does when it would free b
    CHECK(nfreed == 0, "b is not freed yet");
    CHECK(port_reply(&a, M(1)) == 0 && b.msgs_out == 0 && nfreed == 1 && freed[0] == &b,
          "the reply hands b back to the scheduler, to be freed");
    CHECK(port_reply(&a, M(3)) == 0 && nfreed == 1, "a live sender is not handed back");

    // The same when the receiver ends instead of replying.
    reset();
    server = port_create(&a, NULL);
    port_put(&b, server, M(1), 0);
    received(&a, server, M(1), PORT_MESSAGE, 20);
    port_process_exit(&b);
    b.draining = true;
    port_process_exit(&a);
    CHECK(nfreed == 1 && freed[0] == &b && b.msgs_out == 0, "an ending receiver hands back a drained sender");

    // A process that holds its own message, and ends.
    reset();
    server = port_create(&a, NULL);
    replies = port_create(&a, NULL);
    port_put(&a, server, M(1), replies);
    received(&a, server, M(1), PORT_MESSAGE, 10);
    port_process_exit(&a);
    CHECK(a.msgs_out == 0 && port_put(&b, replies, M(2), 0) == -1, "a process that sent to itself ends cleanly");
    CHECK(lock_depth == 0, "the port lock is released");
}

TEST_MAIN(test_names_and_ids, test_put_get_reply, test_capacity, test_signals, test_wait,
          test_delete_returns, test_process_exit)
