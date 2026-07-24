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
    t->pid = 0;
    t->state = TASK_UNUSED;
}

// Run a task in EL0 until it exits, then restore the previous current task.
static void task_run(task_t *t) {
    task_t *prev = current;
    current = t;
    enter_user(t->entry, t->user_sp, t->kctx);  // returns here when t exits
    current = prev;
}

int task_spawn(const char *name) {
    const user_program_t *prog = user_program_find(name);
    if (prog == NULL) {
        printf("spawn: no program named '%s'\n", name);
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
    t->user_sp = (uint64_t)stack + PAGE_SIZE;

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
