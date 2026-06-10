#include "proc/task.h"

static task_t *current;

void task_run(task_t *t) {
    current = t;
    t->state = TASK_RUNNABLE;
    enter_user(t);  // returns here (via kernel_return) when the task exits
}

task_t *task_current(void) {
    return current;
}

void task_exit(int code) {
    if (current) {
        current->state = TASK_EXITED;
        current->exit_code = code;
    }
    kernel_return();
}
