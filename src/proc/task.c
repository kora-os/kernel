#include "proc/task.h"
#include "user/elf.h"
#include "user/embedded.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "lib/printf.h"

static task_t tasks[MAX_TASKS];
static task_t *current;
static int next_pid = 1;

// Grab a free table slot and give it a fresh pid. Returns NULL if the table is
// full. Leaves the slot RUNNABLE; the caller fills in the rest.
static task_t *task_alloc(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->state == TASK_UNUSED) {
            t->pid = next_pid++;
            t->state = TASK_RUNNABLE;
            t->exit_code = 0;
            t->parent = NULL;
            t->entry = 0;
            t->user_sp = 0;
            t->image = NULL;
            t->image_pages = 0;
            t->stack = NULL;
            t->heap = NULL;
            t->heap_pages = 0;
            t->heap_base = 0;
            t->heap_brk = 0;
            t->heap_end = 0;
            t->arg0 = 0;
            t->arg1 = 0;
            return t;
        }
    }
    return NULL;
}

// Release a task's held memory and return its slot to the pool.
static void task_free(task_t *t) {
    if (t->image != NULL) {
        frame_free_pages(t->image, t->image_pages);
        t->image = NULL;
    }
    if (t->stack != NULL) {
        frame_free(t->stack);
        t->stack = NULL;
    }
    if (t->heap != NULL) {
        frame_free_pages(t->heap, t->heap_pages);
        t->heap = NULL;
    }
    t->pid = 0;
    t->state = TASK_UNUSED;
}

// Run a task in EL0 until it exits, then restore the previous current task.
static void task_run(task_t *t) {
    task_t *prev = current;
    current = t;
    enter_user(t->entry, t->user_sp, t->kctx, t->arg0, t->arg1);  // returns on exit
    current = prev;
}

static size_t cstr_len(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void copy_bytes(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

// Lay out argc argument strings and a NULL-terminated argv[] array at the top of
// a task's stack, so the program can be entered as main(argc, argv). On success
// writes the new 16-byte-aligned stack pointer and the address of the argv array
// (both in the task's address space) and returns true; returns false if the
// arguments do not fit in the stack page.
static bool build_args(void *stack, int argc, char *const argv[],
                       uint64_t *out_sp, uint64_t *out_argv) {
    uint64_t base = (uint64_t)stack;
    uint64_t p = base + PAGE_SIZE;
    uint64_t str_addr[MAX_ARGS];

    for (int i = 0; i < argc; i++) {
        size_t len = cstr_len(argv[i]) + 1;
        if (p < base + len) {
            return false;
        }
        p -= len;
        copy_bytes((void *)p, argv[i], len);
        str_addr[i] = p;
    }

    // argv[]: argc + 1 pointers (NULL-terminated), 16-byte aligned.
    p &= ~(uint64_t)15;
    uint64_t need = (uint64_t)(argc + 1) * sizeof(uint64_t);
    if (p < base + need + 16) {
        return false;
    }
    p -= need;
    p &= ~(uint64_t)15;
    uint64_t *arr = (uint64_t *)p;
    for (int i = 0; i < argc; i++) {
        arr[i] = str_addr[i];
    }
    arr[argc] = 0;

    *out_argv = p;
    *out_sp = p - 16;  // leave a small gap below argv[]; stays 16-aligned
    return true;
}

int task_spawn(const char *name, int argc, char *const argv[]) {
    const user_program_t *prog = user_program_find(name);
    if (prog == NULL) {
        // Expected case (e.g. a mistyped shell command); let the caller report it.
        return -1;
    }

    struct loaded_prog lp;
    int rc = elf_load(prog->start, user_program_size(prog), &lp);
    if (rc != 0) {
        printf("spawn: elf_load('%s') failed: %d\n", name, rc);
        return -1;
    }

    void *stack = frame_alloc();
    if (stack == NULL) {
        frame_free_pages(lp.image, lp.image_pages);
        printf("spawn: out of memory for '%s' stack\n", name);
        return -1;
    }

    task_t *t = task_alloc();
    if (t == NULL) {
        frame_free_pages(lp.image, lp.image_pages);
        frame_free(stack);
        printf("spawn: task table full\n");
        return -1;
    }
    t->parent = current;
    t->entry = lp.entry;
    t->image = lp.image;
    t->image_pages = lp.image_pages;
    t->stack = stack;

    uint64_t sp = (uint64_t)stack + PAGE_SIZE;
    uint64_t argv_child = 0;
    if (argc > MAX_ARGS) {
        argc = MAX_ARGS;
    }
    if (argc > 0) {
        if (!build_args(stack, argc, argv, &sp, &argv_child)) {
            printf("spawn: arguments too large for '%s'\n", name);
            task_free(t);  // releases the image and stack we just took
            return -1;
        }
    }
    t->user_sp = sp;
    t->arg0 = (uint64_t)argc;
    t->arg1 = argv_child;

    printf("  [pid %d] run '%s': entry=0x%lx sp=0x%lx (%d image pages)\n",
           t->pid, name, t->entry, t->user_sp, (int)t->image_pages);

    task_run(t);  // suspends us until t exits; t is now an EXITED zombie
    return t->pid;
}

int task_wait(int pid) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->state == TASK_EXITED && t->pid == pid && t->parent == current) {
            int code = t->exit_code;
            task_free(t);
            return code;
        }
    }
    return -1;
}

int task_getpid(void) {
    return current != NULL ? current->pid : 0;
}

task_t *task_current(void) {
    return current;
}

void task_exit(int code) {
    current->state = TASK_EXITED;
    current->exit_code = code;
    kernel_return(current->kctx);  // no return
}

void task_reap_all(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED) {
            task_free(&tasks[i]);
        }
    }
}
