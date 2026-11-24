// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython on KoraOS (project_doc/koracli-plan.md, step 0a): the
// `micropython` program, a REPL on its console with MicroPython's own line
// editor (shared/readline). Ctrl+D on an empty line ends it, Ctrl+A and
// Ctrl+B switch to and from the raw REPL (for tools), Ctrl+E is paste mode.
//
// Its main thread's stack is declared when it is linked (cmake/userfs.cmake,
// MICROPY_KORA_STACK_SIZE bytes): MicroPython's parser and compiler recurse,
// and the stack check keeps a deep recursion a Python RuntimeError instead of
// a write below the stack. The GC heap comes from malloc.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "libk/koraos.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/cstack.h"
#include "py/gc.h"
#include "py/lexer.h"
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
#define HEAP_SIZE (1024 * 1024)

int main(void) {
    mp_cstack_init_with_sp_here(MICROPY_KORA_STACK_SIZE - STACK_MARGIN);
    char *heap = malloc(HEAP_SIZE);
    if (heap == NULL) {
        fputs("micropython: out of memory\n", stderr);
        return 1;
    }
    gc_init(heap, heap + HEAP_SIZE);
    mp_init();
    readline_init0();

    for (;;) {
        if (pyexec_mode_kind == PYEXEC_MODE_RAW_REPL) {
            if (pyexec_raw_repl() != 0) {
                break;
            }
        } else if (pyexec_friendly_repl() != 0) {
            break;
        }
    }

    mp_deinit();
    free(heap);
    return 0;
}

// The roots: the registers and the main thread's stack.
void gc_collect(void) {
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    gc_collect_end();
}

// No files for Python yet (step 0b): nothing to import, run or open.
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)args;
    (void)kwargs;
    mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

void nlr_jump_fail(void *val) {
    fprintf(stderr, "micropython: uncaught exception %p, no handler left\n", val);
    exit(1);
}

// The HAL (py/mphal.h). Output is buffered by the libc and flushed before
// each read, so what was printed is on the screen when a key is awaited.

int mp_hal_stdin_rx_chr(void) {
    fflush(stdout);
    int c = getchar();
    return c == EOF ? CHAR_CTRL_D : c;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    return fwrite(str, 1, len, stdout);
}

// The console starts a new line on '\n' itself (ONLCR), so "cooked" output
// needs no change.
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    fwrite(str, 1, len, stdout);
}

void mp_hal_stdout_tx_str(const char *str) {
    fputs(str, stdout);
}

void mp_hal_stdio_mode_raw(void) {
    fflush(stdout);
    console_mode(CON_RAW);
}

void mp_hal_stdio_mode_orig(void) {
    fflush(stdout);
    console_mode(CON_COOKED);
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(clock() / (CLOCKS_PER_SEC / 1000));
}

void mp_hal_delay_ms(mp_uint_t ms) {
    msleep(ms);
}
