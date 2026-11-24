// SPDX-License-Identifier: GPL-3.0-or-later
//
// EL0 regression fixture for the stacks programs and threads ask for
// (project_doc/libc-plan.md, step 20). It is linked with -z stack-size=65536
// (cmake/userfs.cmake), so its main thread must get 64 KB instead of the one
// page every other program gets.
//
// Each thread finds its own stack with task_info (base and size), then
// recurses through most of it, each frame holding a kilobyte filled with its
// depth and checked on the way back, and checks that its deepest frame lies
// inside the stack it was given. Nothing guards a stack's end, so a stack
// smaller than claimed would overwrite other memory instead of faulting: the
// base check is what catches that.
//
// Prints "stackprobe: main 64 KB, thread 200 KB, default 16 KB" on success;
// a failed check prints itself on stderr and the program exits non-zero.
//
// `stackbig` is the same program linked with a 2 MB request, more than the
// kernel allows: its spawn must fail with a message.
#include "../../user/libk/koraos.h"

_Static_assert(sizeof(struct task_info) == 56, "struct ktask_info's layout in proc/task.h");

#define FRAME_BYTES 1000

static int failed;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "stackprobe: FAILED %s\n", what);
        failed = 1;
    }
}

// The calling thread's stack, from the kernel's task list.
static int own_stack(unsigned long *base, unsigned long *size) {
    struct task_info info;
    int me = gettid();
    for (unsigned i = 0; task_info(i, &info) == 1; i++) {
        if (info.tid == me) {
            *base = info.stack_base;
            *size = info.stack_size;
            return 1;
        }
    }
    return 0;
}

// The lowest frame address of the current dive. Threads dive one at a time.
static unsigned long deepest;

__attribute__((noinline)) static int dive(int depth) {
    volatile unsigned char frame[FRAME_BYTES];
    for (int i = 0; i < FRAME_BYTES; i++) {
        frame[i] = (unsigned char)depth;
    }
    unsigned long here = (unsigned long)frame;
    if (deepest == 0 || here < deepest) {
        deepest = here;
    }
    int ok = depth > 0 ? dive(depth - 1) : 1;
    for (int i = 0; i < FRAME_BYTES; i++) {
        ok &= frame[i] == (unsigned char)depth;
    }
    return ok;  // used after the call: no tail call
}

// Dives `frames` kilobytes deep into the calling thread's stack, which must be
// `want` bytes. Returns the size found, or 0 after reporting a failure.
static unsigned long use_stack(unsigned long want, int frames, const char *who) {
    char what[64];
    unsigned long base = 0, size = 0;
    snprintf(what, sizeof what, "%s: its task_info", who);
    check(own_stack(&base, &size), what);
    snprintf(what, sizeof what, "%s: %lu bytes of stack, want %lu", who, size, want);
    check(size == want, what);
    unsigned long top = base + size, local = (unsigned long)&base;
    snprintf(what, sizeof what, "%s: runs on its own stack", who);
    check(local > base && local < top, what);
    if (failed) {
        return 0;  // diving into a stack smaller than asked for would overwrite other memory
    }
    deepest = 0;
    snprintf(what, sizeof what, "%s: frames intact", who);
    check(dive(frames), what);
    snprintf(what, sizeof what, "%s: %lu KB deep, inside the stack", who, (top - deepest) / 1024);
    check(deepest > base && top - deepest >= (unsigned long)frames * FRAME_BYTES, what);
    return failed ? 0 : size;
}

static long thread_main(void *arg) {
    long *want = arg;
    return (long)use_stack((unsigned long)want[0], (int)want[1], want[2] ? "big thread" : "thread");
}

int main(void) {
    unsigned long main_size = use_stack(64 * 1024, 52, "main");

    long big[3] = {200 * 1024, 175, 1};
    long result = 0;
    int tid = thread_create_stack(thread_main, big, -1, 200 * 1024);
    check(tid > 0 && thread_join(tid, &result) == 0, "a thread with a 200 KB stack");
    unsigned long big_size = (unsigned long)result;

    long normal[3] = {16 * 1024, 12, 0};
    tid = thread_create(thread_main, normal, -1);
    check(tid > 0 && thread_join(tid, &result) == 0, "a thread with the default stack");
    unsigned long normal_size = (unsigned long)result;

    check(thread_create_stack(thread_main, normal, -1, 2ul << 20) == -1,
          "a 2 MB thread stack is refused");
    check(thread_create_stack(thread_main, normal, -1, (1ul << 20) + 1) == -1,
          "one byte over 1 MB is refused");

    if (failed) {
        return 1;
    }
    printf("stackprobe: main %lu KB, thread %lu KB, default %lu KB\n", main_size / 1024,
           big_size / 1024, normal_size / 1024);
    return 0;
}
