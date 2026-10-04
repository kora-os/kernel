// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tasks, their kernel stacks and the (still cooperative, single-core)
// scheduler. See proc/task.h.

#include "proc/task.h"
#include "arch/irq.h"
#include "user/elf.h"
#include "fs/fat32.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "mm/kmalloc.h"
#include "proc/user_mem.h"
#include "lib/printf.h"
#include "lib/string.h"

#define KSTACK_SIZE (KSTACK_PAGES * PAGE_SIZE)
#define KSTACK_FILL 0x6b6b6b6b6b6b6b6bull  // untouched kernel stack words
#define KSTACK_GUARD_WORDS 8               // must still hold the fill on a switch

static task_t tasks[MAX_TASKS];
static int next_pid = 1;
static size_t kstack_peak_max;  // deepest kernel stack use of any reaped task

// The boot thread: kernel_main runs as task 0 on the boot stack. It spawns
// init and waits for it like any parent, but has no user side of its own.
static task_t boot_task = {
    .pid = 0,
    .state = TASK_RUNNABLE,
    .name = "kernel",
};
static task_t *current = &boot_task;

// Grab a free table slot and give it a fresh pid. Returns NULL if the table is
// full. Leaves the slot RUNNABLE; the caller fills in the rest.
static task_t *task_alloc(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        task_t *t = &tasks[i];
        if (t->state == TASK_UNUSED) {
            memset(t, 0, sizeof(*t));
            t->pid = next_pid++;
            t->state = TASK_RUNNABLE;
            return t;
        }
    }
    return NULL;
}

// Release a task's held memory and return its slot to the pool. Never called
// on the running task: its kernel stack is in use.
static void task_free(task_t *t) {
    size_t peak = task_kstack_peak(t);
    if (peak > kstack_peak_max) {
        kstack_peak_max = peak;
    }
    fpsimd_release(t);
    fs_cwd_release(&t->cwd);
    for (int f = 0; f < MAX_OPEN_FILES; f++) {
        if (t->files[f].used) {
            fat32_close(&t->files[f].file);
            t->files[f].used = false;
        }
    }
    if (t->image != NULL) {
        frame_free_pages(t->image, t->image_pages);
        t->image = NULL;
    }
    if (t->stack != NULL) {
        frame_free(t->stack);
        t->stack = NULL;
    }
    if (t->kstack != NULL) {
        frame_free_pages(t->kstack, KSTACK_PAGES);
        t->kstack = NULL;
    }
    user_pages_release_all(t);
    t->pid = 0;
    t->state = TASK_UNUSED;
}

static task_t *parent_of(const task_t *t) {
    return t->parent != NULL ? t->parent : &boot_task;
}

// Scheduling ---------------------------------------------------------------

// The next runnable task after `from` in round-robin order over the boot task
// and the table, or `from` itself if nothing else can run, or NULL.
static task_t *pick_next(task_t *from) {
    int slots = MAX_TASKS + 1;  // slot 0 is the boot task
    int start = from == &boot_task ? 0 : (int)(from - tasks) + 1;
    for (int i = 1; i <= slots; i++) {
        int slot = (start + i) % slots;
        task_t *t = slot == 0 ? &boot_task : &tasks[slot - 1];
        if (t->state == TASK_RUNNABLE) {
            return t;
        }
    }
    return NULL;
}

static void check_kstack(const task_t *t) {
    if (t->kstack == NULL) {
        return;  // the boot task runs on the boot stack
    }
    const uint64_t *guard = t->kstack;
    for (int i = 0; i < KSTACK_GUARD_WORDS; i++) {
        if (guard[i] != KSTACK_FILL) {
            printf("\n*** kernel stack overflow in pid %d (%s) ***\n", t->pid, t->name);
            for (;;) {
                asm volatile("wfi");
            }
        }
    }
}

// Switch to the next runnable task. Returns when the current task is chosen
// again. With nothing runnable, waits for an interrupt to make something so.
// The IRQ mask is not part of the saved context: each task keeps its own in
// `flags`, on its own stack, and gets it back when it is switched in again.
static void schedule(void) {
    uint64_t flags = irq_save();
    task_t *prev = current;
    task_t *next = pick_next(prev);
    while (next == NULL) {
        asm volatile("msr daifclr, #2\n\twfi\n\tmsr daifset, #2" ::: "memory");
        next = pick_next(prev);
    }
    if (next != prev) {
        check_kstack(prev);
        current = next;
        fpsimd_switch_to(next);
        cpu_switch(&prev->ctx, &next->ctx);
    }
    irq_restore(flags);
}

// Block the running task until `child` (or, if NULL, any child) exits.
static void block_on_child(task_t *child) {
    current->state = TASK_BLOCKED;
    current->waiting_for = child;
    schedule();
}

void task_yield(void) {
    schedule();
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

// Read a program off the filesystem and load it into memory via elf_load. The
// file is read into a scratch buffer, which elf_load copies out of, so the
// buffer is freed before returning. Returns 0, or -1 on any failure (a missing
// file is reported silently so the caller -- e.g. the shell -- can react).
static int load_program(const char *name, struct loaded_prog *lp) {
    fs_cwd_t *initial = NULL;
    const fs_cwd_t *cwd = task_current() != NULL ? &current->cwd : NULL;
    if (cwd == NULL) {
        initial = kmalloc(sizeof(*initial));
        if (initial == NULL || fs_boot_cwd(initial) != 0) {
            kfree(initial);
            return -1;
        }
        cwd = initial;
    }
    fat32_file_t f;
    int rc = fs_program_open(cwd, name, &f);
    fs_cwd_release(initial);
    kfree(initial);
    if (rc != 0) return -1;
    if (f.size == 0) {
        fat32_close(&f);
        return -1;
    }

    size_t npages = ((size_t)f.size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *buf = frame_alloc_pages(npages);
    if (buf == NULL) {
        fat32_close(&f);
        printf("spawn: out of memory reading '%s'\n", name);
        return -1;
    }
    uint32_t got = 0;
    while (got < f.size) {
        long n = fat32_read(&f, buf + got, f.size - got);
        if (n <= 0) {
            frame_free_pages(buf, npages);
            fat32_close(&f);
            printf("spawn: read('%s') failed\n", name);
            return -1;
        }
        got += (uint32_t)n;
    }

    rc = elf_load(buf, f.size, lp);
    fat32_close(&f);
    frame_free_pages(buf, npages);
    if (rc != 0) {
        printf("spawn: elf_load('%s') failed: %d\n", name, rc);
        return -1;
    }
    return 0;
}

task_t *task_create(const char *name, int argc, char *const argv[]) {
    struct loaded_prog lp;
    if (load_program(name, &lp) != 0) {
        return NULL;
    }

    void *stack = frame_alloc();
    void *kstack = frame_alloc_pages(KSTACK_PAGES);
    task_t *t = stack != NULL && kstack != NULL ? task_alloc() : NULL;
    if (t == NULL) {
        frame_free_pages(lp.image, lp.image_pages);
        frame_free(stack);
        frame_free_pages(kstack, KSTACK_PAGES);
        printf(stack != NULL && kstack != NULL ? "spawn: task table full\n"
                                               : "spawn: out of memory for '%s' stacks\n",
               name);
        return NULL;
    }
    t->parent = task_current();
    t->image = lp.image;
    t->image_pages = lp.image_pages;
    t->stack = stack;
    t->kstack = kstack;
    size_t n = 0;
    for (; name[n] != '\0' && n < TASK_NAME_MAX - 1; n++) {
        t->name[n] = name[n];
    }
    t->name[n] = '\0';

    int cwd_rc = t->parent != NULL ? fs_cwd_copy(&t->cwd, &t->parent->cwd)
                                   : fs_boot_cwd(&t->cwd);
    if (cwd_rc != 0) {
        task_free(t);
        return NULL;
    }

    uint64_t sp = (uint64_t)stack + PAGE_SIZE;
    uint64_t argv_child = 0;
    if (argc > MAX_ARGS) {
        argc = MAX_ARGS;
    }
    if (argc > 0) {
        if (!build_args(stack, argc, argv, &sp, &argv_child)) {
            printf("spawn: arguments too large for '%s'\n", name);
            task_free(t);  // releases the image and stacks we just took
            return NULL;
        }
    }

    // Fill the kernel stack so its high-water mark can be measured, then put
    // the initial EL0 state at its top: the first switch to the task "returns"
    // through ret_to_user and erets to the entry point with x0/x1 = argc/argv.
    uint64_t *words = kstack;
    for (size_t i = 0; i < KSTACK_SIZE / sizeof(uint64_t); i++) {
        words[i] = KSTACK_FILL;
    }
    t->tf = (struct trapframe *)((uint8_t *)kstack + KSTACK_SIZE - sizeof(struct trapframe));
    memset(t->tf, 0, sizeof(*t->tf));
    t->tf->elr = lp.entry;
    t->tf->sp = sp;
    t->tf->spsr = 0;  // EL0t, interrupts unmasked
    t->tf->regs[0] = (uint64_t)argc;
    t->tf->regs[1] = argv_child;
    t->ctx.lr = (uint64_t)ret_to_user;
    t->ctx.sp = (uint64_t)t->tf;

    printf("  [pid %d] run '%s': entry=0x%lx sp=0x%lx (%d image pages)\n",
           t->pid, name, lp.entry, sp, (int)t->image_pages);
    return t;
}

int task_spawn(const char *name, int argc, char *const argv[]) {
    task_t *child = task_create(name, argc, argv);
    if (child == NULL) {
        return -1;
    }
    int pid = child->pid;
    while (child->state != TASK_EXITED) {
        block_on_child(child);
    }
    return pid;
}

int task_wait(int pid) {
    task_t *me = task_current();
    for (;;) {
        task_t *child = NULL;
        for (int i = 0; i < MAX_TASKS; i++) {
            task_t *t = &tasks[i];
            if (t->state != TASK_UNUSED && t->pid == pid && t->parent == me) {
                child = t;
                break;
            }
        }
        if (child == NULL) {
            return -1;
        }
        if (child->state == TASK_EXITED) {
            int code = child->exit_code;
            task_free(child);
            return code;
        }
        block_on_child(child);
    }
}

int task_getpid(void) {
    return current->pid;
}

task_t *task_current(void) {
    return current != &boot_task ? current : NULL;
}

task_t *task_running(void) {
    return current;
}

void task_exit(int code) {
    task_t *me = current;
    me->state = TASK_EXITED;
    me->exit_code = code;
    fpsimd_release(me);

    task_t *parent = parent_of(me);
    if (parent->state == TASK_BLOCKED &&
        (parent->waiting_for == me || parent->waiting_for == NULL)) {
        parent->state = TASK_RUNNABLE;
        parent->waiting_for = NULL;
    }
    schedule();  // never picks this task again
    for (;;) {
        asm volatile("wfi");
    }
}

void task_reap_all(void) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && &tasks[i] != current) {
            task_free(&tasks[i]);
        }
    }
}

size_t task_kstack_peak(const task_t *t) {
    if (t->kstack == NULL) {
        return 0;
    }
    const uint64_t *words = t->kstack;
    size_t n = KSTACK_SIZE / sizeof(uint64_t);
    size_t i = 0;
    while (i < n && words[i] == KSTACK_FILL) {
        i++;
    }
    return (n - i) * sizeof(uint64_t);
}

size_t task_kstack_peak_max(void) {
    size_t peak = kstack_peak_max;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED && task_kstack_peak(&tasks[i]) > peak) {
            peak = task_kstack_peak(&tasks[i]);
        }
    }
    return peak;
}

void task_for_each(void (*fn)(const task_t *t, void *ctx), void *ctx) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state != TASK_UNUSED) {
            fn(&tasks[i], ctx);
        }
    }
}
