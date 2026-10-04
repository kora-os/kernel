#pragma once

#include "common.h"
#include "fs/fat32.h"
#include "fs/namespace.h"
#include "arch/fpsimd.h"
#include "arch/trapframe.h"

// Process model. Every task has its own kernel stack, with the EL0 register
// state saved in a trap frame at its top, and a saved kernel context (callee-
// saved registers and stack pointer) that cpu_switch() switches between. The
// boot thread is task 0: it is not in the table and has no user side.
//
// Scheduling is still cooperative and single-core: a task runs until it
// blocks (spawn waits for its child, wait for an unreaped child) or exits.
// spawn is create plus wait, so a parent stays suspended until its child
// exits, as before. A finished task becomes a zombie, its memory still held so
// its exit code remains valid, until it is reaped by task_wait() or by
// task_reap_all().

#define MAX_TASKS 8
#define KSTACK_PAGES 4       // 16 KB kernel stack per task
#define MAX_ARGS 16          // most argv entries a spawned program may receive
#define MAX_OPEN_FILES 16    // open file/directory handles per task
#define FD_BASE 3            // fds 0/1/2 are the console; real files start here
#define TASK_NAME_MAX 16

// One open file or directory handle in a task's descriptor table. The FAT32
// handle is a plain value cursor (no external resource), so closing just frees
// the slot.
typedef struct {
    bool used;
    fat32_file_t file;
} open_file_t;

typedef enum {
    TASK_UNUSED = 0,  // free table slot
    TASK_RUNNABLE,    // running, or ready to run
    TASK_BLOCKED,     // waiting for a child to exit
    TASK_EXITED,      // finished but not yet reaped (memory still held)
} task_state_t;

// Kernel context saved by cpu_switch(); layout mirrored in src/arch/entry.S.
struct cpu_context {
    uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28;
    uint64_t fp;  // x29
    uint64_t lr;  // x30: where cpu_switch() returns to
    uint64_t sp;
};

typedef struct task {
    int pid;                          // >0 when in use, 0 when UNUSED (and task 0)
    task_state_t state;
    int exit_code;
    char name[TASK_NAME_MAX];
    struct task *parent;              // who spawned this task (NULL: the kernel)
    struct task *waiting_for;         // BLOCKED: the child, or NULL for any child
    void *image;                      // loaded ELF region (for reclaim)
    size_t image_pages;
    void *stack;                      // user stack region (for reclaim)
    void *kstack;                     // kernel stack (KSTACK_PAGES pages)
    struct trapframe *tf;             // EL0 state, at the top of kstack
    struct cpu_context ctx;           // kernel context while switched out
    struct user_run *runs;            // pages from alloc_pages (proc/user_mem.h)
    fs_cwd_t cwd;                     // inherited per-task filesystem location
    open_file_t files[MAX_OPEN_FILES];  // per-task fd table (indexed fd - FD_BASE)
    bool fp_used;                     // fp holds this task's FP/SIMD state
    struct fpsimd_state fp;           // saved when another task takes the registers
} task_t;

// Load the named program and create a runnable task for it: argv holds argc
// string pointers (readable in the caller's address space), copied onto the
// child's stack and delivered as main(argc, argv). The child does not run
// until the caller blocks or yields. Returns the new task, or NULL.
task_t *task_create(const char *name, int argc, char *const argv[]);

// Create a task and block until it exits (spawn's historical semantics: the
// child runs to completion first). Returns the child's pid, or -1 on failure.
// The child stays an unreaped zombie until task_wait() collects it.
int task_spawn(const char *name, int argc, char *const argv[]);

// Reap a child of the current task by pid, blocking until it has exited: free
// its memory and return its exit code. Returns -1 if there is no such child.
int task_wait(int pid);

// pid of the current task (0 for the kernel's own boot task).
int task_getpid(void);

// The current user task, or NULL while the kernel's boot task runs.
task_t *task_current(void);

// The running task, including the boot task (never NULL).
task_t *task_running(void);

// Mark the current task exited, wake a waiting parent and switch away for good.
void task_exit(int code) __attribute__((noreturn));

// Give up the CPU to another runnable task, if there is one.
void task_yield(void);

// Release every task-table slot and its held memory. The kernel calls this to
// tear down after the top-level task and any descendants finish.
void task_reap_all(void);

// Kernel stack bytes a task has used at most (from the untouched fill pattern),
// and the most any task has used since boot.
size_t task_kstack_peak(const task_t *t);
size_t task_kstack_peak_max(void);

// Visit every live task (for diagnostics).
void task_for_each(void (*fn)(const task_t *t, void *ctx), void *ctx);

// --- implemented in src/arch/entry.S ---
// Save the callee-saved context into *from and resume *to.
void cpu_switch(struct cpu_context *from, struct cpu_context *to);
// First return of a new task: restore its trap frame (at sp) and eret to EL0.
void ret_to_user(void);
