// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: message ports between processes (ipc/port.h).
//   portprobe          runs every case below against children of its own,
//                      one "portprobe: <case>" line each, and
//                      "portprobe: all passed" (exit 0) or a FAIL line (exit 1)
//   portprobe client   a child that sends to the parent's port "portprobe"
//   portprobe holder   a child that takes a message and ends without replying
//   portprobe waiter   a child that waits on its port until it is killed
#include "../../user/libk/koraos.h"

enum { OP_DOUBLE = 1, OP_HOLD, OP_DONE };

struct request {
    int op;
    int value;
};

#define ROUND_TRIPS 100
#define PIPELINE 8

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        printf("portprobe: FAIL %s\n", what);
        failures++;
    }
}

static int spawn_child(const char *mode) {
    char *argv[] = {"portprobe", (char *)mode};
    return spawn_flags("portprobe", 2, argv, SPAWN_NOWAIT);
}

// The port another process creates, once it exists.
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

static int client(void) {
    int server = find_port("portprobe");
    int replies = port_create(NULL);
    if (server < 0 || replies < 0) {
        return 1;
    }
    struct port_message got;
    static struct request one;
    for (int i = 0; i < ROUND_TRIPS; i++) {
        one = (struct request){OP_DOUBLE, i};
        if (port_put(server, &one, replies) != 0 || port_wait(replies, &got) != 1 ||
            got.kind != PORT_REPLY || got.msg != &one || one.value != 2 * i) {
            return 2;
        }
    }
    static struct request many[PIPELINE];
    for (int i = 0; i < PIPELINE; i++) {
        many[i] = (struct request){OP_DOUBLE, 100 + i};
        if (port_put(server, &many[i], replies) != 0) {
            return 3;
        }
    }
    for (int i = 0; i < PIPELINE; i++) {
        if (port_wait(replies, &got) != 1 || got.msg != &many[i] || many[i].value != 200 + 2 * i) {
            return 4;
        }
    }
    // A message the server keeps: this program ends while it is held.
    static struct request held = {OP_HOLD, 0};
    static struct request done = {OP_DONE, 0};
    if (port_put(server, &held, replies) != 0 || port_put(server, &done, replies) != 0 ||
        port_wait(replies, &got) != 1 || got.msg != &done) {
        return 5;
    }
    return 0;
}

static int holder(void) {
    int port = port_create("portprobe.h");
    struct port_message got;
    if (port < 0 || port_wait(port, &got) != 1 || got.kind != PORT_MESSAGE) {
        return 1;
    }
    return 3;  // ends holding it
}

static int waiter(void) {
    int port = port_create("portprobe.w");
    struct port_message got;
    port_wait(port, &got);  // killed while it sleeps
    return 1;
}

// --- The cases -----------------------------------------------------------------

static struct sysinfo baseline;

static unsigned procs_now(void) {
    struct sysinfo info;
    sysinfo(&info);
    return info.procs;
}

static void serve_client(void) {
    int port = port_create("portprobe");
    check(port > 0, "create a named port");
    check(port_create("portprobe") == -1, "a second port of the same name");
    check(port_find("portprobe") == port, "find a port by name");
    int pid = spawn_child("client");
    check(pid > 0, "start the client");
    struct port_message got;
    struct request *held = NULL;
    int served = 0;
    for (;;) {
        if (port_wait(port, &got) != 1 || got.kind != PORT_MESSAGE || got.sender != pid) {
            check(0, "a message from the client");
            break;
        }
        struct request *r = got.msg;
        if (r->op == OP_HOLD) {
            held = r;
            continue;
        }
        if (r->op == OP_DOUBLE) {
            r->value *= 2;
            served++;
        }
        port_reply(r);
        if (r->op == OP_DONE) {
            break;
        }
    }
    int code = -1;
    check(waitpid(pid, &code, 0) == pid && code == 0, "the client checked every reply");
    check(served == ROUND_TRIPS + PIPELINE, "every request served");
    printf("portprobe: %d round trips and %d pipelined requests\n", ROUND_TRIPS, PIPELINE);

    // The client ended and was reaped, but the server holds one of its
    // messages: its memory, and its process slot, stay until the reply.
    check(held != NULL, "the client's held message");
    check(procs_now() == baseline.procs + 1, "an ended sender stays while its message is held");
    check(port_reply(held) == 0, "reply to an ended sender");
    check(procs_now() == baseline.procs, "and it is freed by the reply");
    struct sysinfo after;
    sysinfo(&after);
    check(after.pages_free == baseline.pages_free, "every page of the client is back");
    printf("portprobe: an ended sender lasts until its held message is replied\n");
    check(port_delete(port) == 0 && port_find("portprobe") == -1, "delete the port");
}

static void lose_to_holder(void) {
    int pid = spawn_child("holder");
    int port = find_port("portprobe.h");
    int replies = port_create(NULL);
    static struct request r = {OP_DOUBLE, 21};
    struct port_message got;
    check(port > 0 && replies > 0 && port_put(port, &r, replies) == 0, "send to the holder");
    check(port_wait(replies, &got) == 1 && got.kind == PORT_RETURNED && got.msg == &r && r.value == 21,
          "a message whose receiver ends comes back unhandled");
    int code = -1;
    check(waitpid(pid, &code, 0) == pid && code == 3, "the holder ended");
    check(port_find("portprobe.h") == -1, "its port went with it");
    check(port_put(port, &r, 0) == -1, "its old id names nothing");
    port_delete(replies);
    printf("portprobe: a receiver that ends returns what it held\n");
}

static void kill_waiter(void) {
    int pid = spawn_child("waiter");
    check(find_port("portprobe.w") > 0, "the waiter's port");
    msleep(20);  // let it go to sleep in port_wait
    check(kill(pid) == 0, "kill the waiter");
    int code = 0;
    check(waitpid(pid, &code, 0) == pid && code == EXIT_KILLED, "a program asleep in port_wait can be killed");
    check(port_find("portprobe.w") == -1, "its port went with it");
    printf("portprobe: a waiter can be killed\n");
}

int main(int argc, char **argv) {
    if (argc > 1) {
        if (!strcmp(argv[1], "client")) {
            return client();
        }
        if (!strcmp(argv[1], "holder")) {
            return holder();
        }
        if (!strcmp(argv[1], "waiter")) {
            return waiter();
        }
        return 2;
    }
    sysinfo(&baseline);
    check(port_find("nothing") == -1 && port_put(12345, &baseline, 0) == -1 &&
              port_reply(&baseline) == -1 && port_create("sixteen-bytes-xx") == -1,
          "bad names, ids and replies are refused");
    serve_client();
    lose_to_holder();
    kill_waiter();
    if (failures == 0) {
        printf("portprobe: all passed\n");
    }
    return failures != 0;
}
