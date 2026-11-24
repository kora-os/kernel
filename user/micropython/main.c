// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython on KoraOS (project_doc/koracli-plan.md, steps 0a and 0b): the
// `micropython` program.
//
//   micropython                  a REPL on its console, with MicroPython's own
//                                line editor (shared/readline): Ctrl+D on an
//                                empty line ends it, Ctrl+E is paste mode,
//                                Ctrl+A and Ctrl+B the raw REPL and back
//   micropython SCRIPT [ARG...]  runs SCRIPT, with sys.argv [SCRIPT, ARG...]:
//                                0, 1 for an exception, 130 for Ctrl+C, or
//                                what sys.exit() gave
//
// sys.path is the script's directory ("" at the REPL: the current one), then
// sys:python. Ctrl+C is the program's (it allocates SIG_BREAK): it raises
// KeyboardInterrupt (hal.c); a second one while the first is still pending
// ends the program, as for any program.
//
// The GC heap grows from MICROPY_KORA_HEAP_FIRST bytes (mpconfigport.h).
// The main thread's stack is declared when it is linked (cmake/userfs.cmake);
// the stack check is set up from what the kernel gave, so a deep recursion
// is a Python RuntimeError instead of a write below the stack.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libk/koraos.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/readline/readline.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"

#ifndef MICROPY_KORA_STACK_SIZE
#error "MICROPY_KORA_STACK_SIZE: the stack the program is linked with"
#endif

// Room kept below the stack check's limit for the C code that runs after it.
#define STACK_MARGIN (8 * 1024)

// The main thread's stack, from the kernel's task list: its top and size.
static void stack_init(void) {
    struct task_info info;
    int me = gettid();
    for (unsigned i = 0; task_info(i, &info) == 1; i++) {
        if (info.tid == me && info.stack_size > STACK_MARGIN) {
            mp_cstack_init_with_top((void *)(info.stack_base + info.stack_size),
                                    info.stack_size - STACK_MARGIN);
            return;
        }
    }
    mp_cstack_init_with_sp_here(MICROPY_KORA_STACK_SIZE - STACK_MARGIN);
}

static mp_obj_t new_str(const char *s, size_t n) {
    return mp_obj_new_str(s, n);
}

int main(int argc, char **argv) {
    stack_init();
    char *heap = malloc(MICROPY_KORA_HEAP_FIRST);
    if (heap == NULL) {
        fputs("micropython: out of memory\n", stderr);
        return 1;
    }
    gc_init(heap, heap + MICROPY_KORA_HEAP_FIRST);
    sig_alloc(SIG_BREAK);  // Ctrl+C is KeyboardInterrupt (hal.c)
    mp_init();
    readline_init0();

    // sys.path: mp_init put "" first; the library on the userfs after it.
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("sys:python")));

    int code;
    if (argc > 1) {
        const char *script = argv[1];
        for (int i = 1; i < argc; i++) {
            mp_obj_list_append(mp_sys_argv, new_str(argv[i], strlen(argv[i])));
        }
        // The script's directory comes first, as for CPython: up to its last
        // '/', or its volume or assign with the ':'.
        const char *slash = strrchr(script, '/');
        const char *colon = strrchr(script, ':');
        size_t dir = slash != NULL && slash > colon ? (size_t)(slash - script)
                     : colon != NULL               ? (size_t)(colon - script + 1)
                                                   : 0;
        mp_obj_list_t *path = MP_OBJ_TO_PTR(mp_sys_path);
        path->items[0] = dir > 0 ? new_str(script, dir) : MP_OBJ_NEW_QSTR(MP_QSTR_);
        code = pyexec_file(script) & 0xff;
    } else {
        code = 0;
        for (;;) {
            int ret;
            if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
                ret = pyexec_raw_repl();
            } else {
                ret = pyexec_friendly_repl();
            }
            if (ret & PYEXEC_FORCED_EXIT) {
                code = ret & 0xff;  // Ctrl+D, or sys.exit()
                break;
            }
        }
    }

    fflush(stdout);
    mp_deinit();
    return code;
}

// The roots: the registers and the main thread's stack.
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    fprintf(stderr, "micropython: uncaught exception %p, no handler left\n", val);
    exit(1);
}
