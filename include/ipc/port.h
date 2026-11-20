// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Message ports, as in AmigaOS Exec: a queue of messages and a wake-up for the
// process that owns the port. A message is a block of the sender's own memory
// (there is no memory protection, so a pointer is enough): the kernel queues
// the pointer and never reads or writes the message. The receiver takes it
// with port_get() or port_wait(), handles it, and gives it back with
// port_reply(), which queues it on the reply port the sender named, or simply
// releases it when there is none. Until then it must not touch the message
// again; the sender must not reuse it before it comes back.
//
// Ports are known by an id, which carries a generation: the id of a deleted
// port stays invalid when its slot is reused. A port may also have a public
// name for others to find it (port_find). Any process may put a message on
// any port; only the owner's threads may take messages, wait or delete it.
//
// When a process ends (its last thread exits), its ports are deleted: the
// messages queued on them go back to their senders, marked PORT_RETURNED. Its
// own messages still queued elsewhere are withdrawn. Those a receiver already
// holds cannot be: the process keeps its memory, and its slot, until they
// have all been replied (port_messages_out), so a receiver never touches
// freed memory. Messages a dying receiver holds go back to their senders,
// marked PORT_RETURNED.
//
// Everything here runs under the big kernel lock.

struct process;

#define PORT_NAME_MAX 16     // including the terminating NUL
#define MAX_PORTS 32         // system-wide
#define MAX_PORT_MESSAGES 128  // messages in flight, system-wide

// What port_get() and port_wait() hand out.
#define PORT_MESSAGE 1   // a message for this port's owner: reply to it
#define PORT_REPLY 2     // one of the owner's own, back from its receiver
#define PORT_RETURNED 3  // one of the owner's own, back unhandled: its port was
                         // deleted, or its receiver ended, first

// Mirrors the userland struct port_message (user/libk/koraos.h).
struct port_received {
    uint64_t msg;
    int32_t kind;    // PORT_MESSAGE, PORT_REPLY or PORT_RETURNED
    int32_t sender;  // pid of the process that sent it
};

// A new port owned by `me`, with a public name, or anonymous for NULL or "".
// Returns its id (> 0), or -1: a name already in use or too long, or no free
// port.
int port_create(struct process *me, const char *name);

// Delete one of `me`'s ports, returning its queued messages to their senders.
// Returns 0, or -1 if `id` is not a port of `me`.
int port_delete(struct process *me, int id);

// The id of the port named `name`, or -1.
int port_find(const char *name);

// Whether `id` names a port.
bool port_exists(int id);

// Queue `msg` on port `id`, from `me`. `reply` is the port it should come back
// to: one of `me`'s, another's, or 0 for none. Returns 0, or -1: no such port
// (or reply port), msg is 0 or already in flight from `me`, or too many
// messages are in flight.
int port_put(struct process *me, int id, uint64_t msg, int reply);

// Take the oldest message queued on `me`'s port `id`. Returns 1 with *out
// filled, 0 if there is none, or -1 if `id` is not a port of `me`.
int port_get(struct process *me, int id, struct port_received *out);

// As port_get(), but sleep until a message arrives. Returns 1, or -1 if `id`
// is not (or stops being) a port of `me`, or the waiting task was killed.
int port_wait(struct process *me, int id, struct port_received *out);

// Give back `msg`, which `me` received as a PORT_MESSAGE: it goes to its
// reply port as a PORT_REPLY, or is released if that port is gone or there is
// none. Returns 0, or -1 if `me` holds no such message.
int port_reply(struct process *me, uint64_t msg);

// The last thread of `p` exited: delete its ports, withdraw its queued
// messages, return those it holds.
void port_process_exit(struct process *p);

// Messages `p` sent that receivers hold and have not replied yet.
unsigned port_messages_out(const struct process *p);

// Provided by the scheduler: the last message `p` had out came back after
// `p` ended. It may free `p` now.
void proc_messages_back(struct process *p);
