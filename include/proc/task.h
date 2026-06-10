#pragma once

#include "common.h"

typedef enum {
    TASK_RUNNABLE,
    TASK_EXITED,
} task_state_t;

// Minimal single-task control block. The first two fields (entry, user_sp)
// are read directly by enter_user in src/arch/entry.S at offsets 0 and 8 --
// keep them first.
typedef struct task {
    uint64_t entry;       // EL0 entry point
    uint64_t user_sp;     // EL0 stack top
    task_state_t state;
    int exit_code;
} task_t;

// Run a task: enters EL0 and returns here only when the task calls exit().
void task_run(task_t *t);

// The task currently executing in EL0 (NULL if none).
task_t *task_current(void);

// Mark the current task exited and return control to the kernel (no return).
void task_exit(int code);

// --- implemented in src/arch/entry.S ---
// Save kernel context, drop to EL0 at t->entry with SP_EL0 = t->user_sp.
void enter_user(task_t *t);
// Restore the kernel context saved by enter_user (used to unwind out of EL0).
void kernel_return(void) __attribute__((noreturn));
