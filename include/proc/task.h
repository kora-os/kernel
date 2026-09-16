#pragma once

#include "common.h"
#include "fs/fat32.h"

// Cooperative, one-at-a-time process model. Tasks nest: spawning a program
// suspends the caller and runs the child to completion in EL0, then resumes the
// caller. A finished task becomes a zombie -- its image and stack stay allocated
// so its exit code (and its distinct load address) remain valid -- until it is
// reaped by task_wait() or by task_reap_all().

#define MAX_TASKS 8
#define TASK_KCTX_WORDS 13   // x19..x30 (12) + sp; see src/arch/entry.S
#define USER_HEAP_PAGES 16   // 64 KB per-task heap, allocated lazily on first sbrk
#define MAX_ARGS 16          // most argv entries a spawned program may receive
#define MAX_OPEN_FILES 16    // open file/directory handles per task
#define FD_BASE 3            // fds 0/1/2 are the console; real files start here

// One open file or directory handle in a task's descriptor table. The FAT32
// handle is a plain value cursor (no external resource), so closing just frees
// the slot.
typedef struct {
    bool used;
    fat32_file_t file;
} open_file_t;

typedef enum {
    TASK_UNUSED = 0,  // free table slot
    TASK_RUNNABLE,    // created / running
    TASK_EXITED,      // finished but not yet reaped (memory still held)
} task_state_t;

typedef struct task {
    int pid;                          // >0 when in use, 0 when UNUSED
    task_state_t state;
    int exit_code;
    struct task *parent;              // who spawned this task (NULL for init)
    uint64_t entry;                   // EL0 entry point
    uint64_t user_sp;                 // EL0 stack top
    void *image;                      // loaded ELF region (for reclaim)
    size_t image_pages;
    void *stack;                      // user stack region (for reclaim)
    void *heap;                       // heap region, or NULL until first sbrk
    size_t heap_pages;
    uint64_t heap_base;               // heap bounds; brk moves within [base, end]
    uint64_t heap_brk;
    uint64_t heap_end;
    uint64_t arg0;                    // EL0 entry x0 (argc)
    uint64_t arg1;                    // EL0 entry x1 (argv, in the task's stack)
    open_file_t files[MAX_OPEN_FILES];  // per-task fd table (indexed fd - FD_BASE)
    uint64_t kctx[TASK_KCTX_WORDS];   // kernel context saved by enter_user
} task_t;

// Load the named embedded program, create a task, and run it to completion in
// EL0 (the caller is suspended until it exits). argv holds argc string pointers
// (readable in the caller's address space); they are copied onto the child's
// stack and delivered as main(argc, argv). Pass argc == 0 for no arguments.
// Returns the new pid, or -1 on failure. The task lingers as an unreaped zombie
// until task_wait() collects it.
int task_spawn(const char *name, int argc, char *const argv[]);

// Reap an exited child of the current task by pid: free its memory and return
// its exit code. Returns -1 if there is no matching exited child.
int task_wait(int pid);

// pid of the currently running task (0 if none).
int task_getpid(void);

// The currently running task, or NULL if none.
task_t *task_current(void);

// Mark the current task exited and unwind back to whoever ran it (no return).
void task_exit(int code) __attribute__((noreturn));

// Release every task-table slot and its held memory. The kernel calls this to
// tear down after the top-level task and any descendants finish.
void task_reap_all(void);

// --- implemented in src/arch/entry.S ---
// Save the kernel's callee-saved context into kctx, then drop to EL0 at entry
// with SP_EL0 = user_sp and x0/x1 = argc/argv.
void enter_user(uint64_t entry, uint64_t user_sp, uint64_t *kctx, uint64_t argc,
                uint64_t argv);
// Restore a context saved by enter_user (unwinds out of EL0 to its caller).
void kernel_return(uint64_t *kctx) __attribute__((noreturn));
