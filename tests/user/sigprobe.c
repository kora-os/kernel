// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: signals (proc/task.h), alone and with ports and children.
//   sigprobe          runs every case below, one "sigprobe: <case>" line each,
//                     and "sigprobe: all passed" (exit 0) or a FAIL line (exit 1)
//   sigprobe sender   a child that sends SENT messages to the parent's port
//                     "sigprobe", each waiting for its reply
//   sigprobe waiter   a child that sleeps in sig_wait until it is killed
#include "../../user/libk/koraos.h"

#define SENT 50
#define FIRST_BIT 8  // bits below are kept for the system

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("sigprobe: FAIL %s\n", what);
        failures++;
    }
}

static int find_port(const char *name) {
    for (int i = 0; i < 1000; i++) {
        int port = port_find(name);
        if (port > 0) {
            return port;
        }
        msleep(1);
    }
    return -1;
}

// --- The children --------------------------------------------------------------

static int sender(void) {
    int server = find_port("sigprobe");
    int replies = port_create(NULL);
    if (server < 0 || replies < 0) {
        return 1;
    }
    static int value;
    struct port_message got;
    for (int i = 0; i < SENT; i++) {
        value = i;
        if (port_put(server, &value, replies) != 0 || port_wait(replies, &got) != 1 ||
            got.kind != PORT_REPLY || value != i + 1000) {
            return 1;
        }
    }
    return 0;
}

static int waiter(void) {
    int bit = sig_alloc(-1);
    sig_wait(SIG_MASK(bit));  // nobody signals it: killed while it sleeps
    return 1;
}

// --- The cases -----------------------------------------------------------------

static void allocation(void) {
    int high = sig_alloc(-1), next = sig_alloc(-1);
    check(high == 31 && next == 30, "sig_alloc(-1) hands out the highest free bits");
    check(sig_alloc(31) == -1, "a bit is allocated once");
    // Bit 0, the break, is the one system bit a program may take (breakprobe
    // covers it): its allocation is not tried here.
    check(sig_alloc(FIRST_BIT - 1) == -1 && sig_alloc(1) == -1 && sig_alloc(32) == -1,
          "system bits and bits past 31 are refused");
    check(sig_alloc(FIRST_BIT) == FIRST_BIT, "a free bit is allocated by number");
    int count = 3;
    while (sig_alloc(-1) >= 0) {
        count++;
    }
    check(count == 32 - FIRST_BIT, "every user bit can be allocated, and no more");
    for (int bit = FIRST_BIT; bit < 32; bit++) {
        check(sig_free(bit) == 0, "free each bit");
    }
    check(sig_free(31) == -1 && sig_free(FIRST_BIT - 1) == -1, "only allocated bits are freed");

    int a = sig_alloc(-1), b = sig_alloc(-1);
    unsigned both = SIG_MASK(a) | SIG_MASK(b);
    check((sig_set(0, 0) & both) == 0, "new bits start clear");
    check(sig_send(getpid(), SIG_MASK(a)) == 0 && (sig_set(0, 0) & both) == SIG_MASK(a),
          "a thread signals itself");
    check(sig_wait(both) == SIG_MASK(a) && (sig_set(0, 0) & both) == 0,
          "a wait returns a signal already set at once, and clears it");
    sig_send(getpid(), SIG_MASK(a));
    sig_send(getpid(), SIG_MASK(a));
    check(sig_wait(SIG_MASK(a)) == SIG_MASK(a) && (sig_set(0, 0) & SIG_MASK(a)) == 0,
          "a signal set twice counts once");
    check(sig_set(both, both) == 0 && (sig_set(0, SIG_MASK(a)) & both) == both &&
              (sig_set(0, 0) & both) == SIG_MASK(b), "sig_set sets and clears, returning the old ones");
    check(sig_wait(SIG_MASK(b)) == SIG_MASK(b), "and a signal it set is waited for");
    check(sig_wait(0) == 0, "an empty mask returns at once");
    check(sig_send(1 << 30, SIG_MASK(a)) == -1, "no such thread");
    sig_send(getpid(), SIG_MASK(a));
    check(sig_free(a) == 0 && sig_alloc(a) == a && (sig_set(0, 0) & SIG_MASK(a)) == 0,
          "a freed bit comes back clear");
    sig_free(a);
    sig_free(b);
    printf("sigprobe: allocation, set, clear and wait\n");
}

static int woken_bit;
static int waker_tid;

static long wake_main(void *arg) {
    waker_tid = gettid();
    msleep(30);
    sig_send((int)(long)arg, SIG_MASK(woken_bit));
    return 0;
}

static void thread_wakes(void) {
    woken_bit = sig_alloc(-1);
    int other = sig_alloc(-1);
    // Any core: with several, the signal crosses cores.
    int tid = thread_create(wake_main, (void *)(long)getpid(), -1);
    unsigned long start = uptime_us();
    unsigned got = sig_wait(SIG_MASK(woken_bit) | SIG_MASK(other));
    unsigned long slept = uptime_us() - start;
    check(tid > 0 && thread_join(tid, NULL) == 0, "the waking thread");
    check(waker_tid == tid && gettid() == getpid(), "gettid: a thread's id, the pid in main");
    check(got == SIG_MASK(woken_bit), "only the signal that was sent is returned");
    check(slept >= 20000, "the waiter slept until it came");
    sig_free(woken_bit);
    sig_free(other);
    printf("sigprobe: a thread wakes another\n");
}

static void port_and_child(void) {
    int port = port_create("sigprobe");
    int port_bit = sig_alloc(-1), end_bit = sig_alloc(-1), unused = sig_alloc(-1);
    sig_free(unused);
    check(port_signal(port, unused) == -1 && port_signal(12345, port_bit) == -1,
          "a port is tied to an allocated bit, and to one of this program's ports");
    check(spawn_flags("sigprobe", 1, (char *[]){"sigprobe"}, SPAWN_NOWAIT | SPAWN_SIGNAL(unused)) == -1,
          "a child's end signals an allocated bit only");

    static int own;
    check(port_put(port, &own, 0) == 0 && port_signal(port, port_bit) == 0 &&
              (sig_set(0, 0) & SIG_MASK(port_bit)) != 0, "a port tied with a message waiting signals at once");
    struct port_message got;
    check(port_get(port, &got) == 1 && got.msg == &own && port_reply(&own) == 0, "take it");
    sig_set(0, SIG_MASK(port_bit));

    char *argv[] = {"sigprobe", "sender"};
    int pid = spawn_flags("sigprobe", 2, argv, SPAWN_NOWAIT | SPAWN_SIGNAL(end_bit));
    check(pid > 0, "spawn the sender");
    int served = 0, wakes = 0, code = -1;
    int ended = 0;
    while (pid > 0 && !ended) {
        unsigned signals = sig_wait(SIG_MASK(port_bit) | SIG_MASK(end_bit));
        wakes++;
        while (port_get(port, &got) == 1) {
            int *value = got.msg;
            *value += 1000;
            port_reply(value);
            served++;
        }
        if (signals & SIG_MASK(end_bit)) {
            ended = waitpid(pid, &code, WNOHANG) == pid;
            check(ended, "its end signal comes once it can be reaped");
        }
    }
    check(served == SENT && code == 0, "every message was served, and the sender passed");
    check(wakes <= SENT + 1, "one wake-up per message at most, plus its end");

    check(sig_free(port_bit) == 0 && port_put(port, &own, 0) == 0 &&
              (sig_set(0, 0) & SIG_MASK(port_bit)) == 0, "a freed bit unties its port");
    port_delete(port);
    sig_free(end_bit);
    printf("sigprobe: %d messages and a child's end on two signals\n", SENT);
}

static void kill_waiter(void) {
    char *argv[] = {"sigprobe", "waiter"};
    int pid = spawn_flags("sigprobe", 2, argv, SPAWN_NOWAIT);
    msleep(20);  // let it go to sleep in sig_wait
    check(pid > 0 && kill(pid) == 0, "kill the waiter");
    int code = 0;
    check(waitpid(pid, &code, 0) == pid && code == EXIT_KILLED, "a program asleep in sig_wait can be killed");
    printf("sigprobe: a waiter can be killed\n");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "sender")) {
            return sender();
        }
        if (!strcmp(argv[1], "waiter")) {
            return waiter();
        }
        return 2;
    }
    allocation();
    thread_wakes();
    port_and_child();
    kill_waiter();
    if (failures == 0) {
        printf("sigprobe: all passed\n");
    }
    return failures != 0;
}
