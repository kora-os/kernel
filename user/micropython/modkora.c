// SPDX-License-Identifier: GPL-3.0-or-later
//
// The `_kora` module: KoraOS from Python, in C. Programs import `kora`
// (sys:python/kora.py), which takes all of this and adds what is better in
// Python: run(), capture(), status and the cmd wrappers.
//
//   uptime(), volumes(), assigns()   the system (KoraCLI step 0b)
//   key()                            the next key press, in mode(EVENTS): a
//                                    (char, key, modifiers) tuple; Ctrl+C or
//                                    ESC as a break raise KeyboardInterrupt
//   mode([m])                        the console's input mode: COOKED, RAW or
//                                    EVENTS
//   bar(which, text)                 a bar (TOP or BOTTOM) above or below the
//                                    console's grid; None takes it away
//   esc_break(on)                    ESC as a break (KeyboardInterrupt) for us
//   size()                           the console's (columns, rows), or None
//   repl()                           MicroPython's own REPL, until Ctrl+D
//   execute(argv, ...)               a program run, its output captured or
//                                    not, or started in the background
//   reap()                           background jobs that ended
//   kill(pid)                        end a program
//   assign(name[, target])           set an assign, or (no target) remove it
//
// The console parts are KoraCLI's (step 1b, on step 1a's console); running
// programs is step 1c.
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "libk/koraos.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

// A fixed-size field's text: up to its NUL, or all of it.
static mp_obj_t field_str(const char *s, size_t size) {
    const char *end = memchr(s, '\0', size);
    return mp_obj_new_str(s, end != NULL ? (size_t)(end - s) : size);
}

// uptime(): seconds since boot, a float.
static mp_obj_t kora_uptime(void) {
    return mp_obj_new_float((mp_float_t)clock() / CLOCKS_PER_SEC);
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_uptime_obj, kora_uptime);

// volumes(): a (device, label, boot, read_only) tuple for each volume, as the
// shell's `volumes` lists them.
static mp_obj_t kora_volumes(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    struct volume_info v;
    for (unsigned i = 0; volume_info(i, &v) == 1; i++) {
        mp_obj_t items[4] = {
            field_str(v.device, sizeof v.device),
            field_str(v.label, sizeof v.label),
            mp_obj_new_bool(v.flags & VOLUME_BOOT),
            mp_obj_new_bool(v.flags & VOLUME_READ_ONLY),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(4, items));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_volumes_obj, kora_volumes);

// assigns(): a (name, target, fixed) tuple for each assign, as `assign`
// lists them; a fixed one (sys:) cannot be changed or removed.
static mp_obj_t kora_assigns(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    static struct assign_info a;  // 4 KB of target: kept off the stack
    for (unsigned i = 0; assign_info(i, &a) == 1; i++) {
        mp_obj_t items[3] = {
            field_str(a.name, sizeof a.name),
            field_str(a.target, sizeof a.target),
            mp_obj_new_bool(a.flags & ASSIGN_IMMUTABLE),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(3, items));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_assigns_obj, kora_assigns);

static void check(int result) {
    if (result != 0) {
        mp_raise_OSError(MP_EINVAL);
    }
}

// key(wait=True): the next key press, waiting for it; key(False) gives None
// when no key has come.
static mp_obj_t kora_key(size_t n_args, const mp_obj_t *args) {
    struct con_key_event event;
    int got = kora_read_key(&event, n_args == 0 || mp_obj_is_true(args[0]));
    if (got == -3) {
        return mp_const_none;
    }
    if (got == -1) {
        mp_raise_type(&mp_type_KeyboardInterrupt);
    }
    if (got == 0) {
        mp_raise_type(&mp_type_EOFError);
    }
    if (got < 0) {
        mp_raise_OSError(MP_EINVAL);  // not a console, or not in EVENTS mode
    }
    // The character as a str of one code point (UTF-8 inside).
    char utf8[4];
    size_t n = 0;
    uint32_t c = event.codepoint;
    if (c >= 0xD800 && c < 0xE000) {
        c = 0;  // a surrogate is no character
    }
    if (c == 0) {
    } else if (c < 0x80) {
        utf8[n++] = (char)c;
    } else if (c < 0x800) {
        utf8[n++] = (char)(0xC0 | (c >> 6));
        utf8[n++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        utf8[n++] = (char)(0xE0 | (c >> 12));
        utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
        utf8[n++] = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x110000) {
        utf8[n++] = (char)(0xF0 | (c >> 18));
        utf8[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
        utf8[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
        utf8[n++] = (char)(0x80 | (c & 0x3F));
    }
    mp_obj_t ch = mp_obj_new_str(utf8, n);
    mp_obj_t items[3] = {ch, MP_OBJ_NEW_SMALL_INT(event.keycode),
                         MP_OBJ_NEW_SMALL_INT(event.modifiers)};
    return mp_obj_new_tuple(3, items);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kora_key_obj, 0, 1, kora_key);

static mp_obj_t kora_mode(size_t n_args, const mp_obj_t *args) {
    if (n_args == 0) {
        return MP_OBJ_NEW_SMALL_INT(kora_console_get_mode());
    }
    check(kora_console_mode(mp_obj_get_int(args[0])));
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kora_mode_obj, 0, 1, kora_mode);

static mp_obj_t kora_bar(mp_obj_t which_in, mp_obj_t text_in) {
    int which = mp_obj_get_int(which_in);
    if (text_in == mp_const_none) {
        check(__con_bar(which, NULL, 0));
    } else {
        size_t length;
        const char *text = mp_obj_str_get_data(text_in, &length);
        check(__con_bar(which, text, length));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(kora_bar_obj, kora_bar);

static bool esc_on;  // ESC as a break, as last asked for

static mp_obj_t kora_esc_break(mp_obj_t on) {
    check(console_esc_break(mp_obj_is_true(on)));
    esc_on = mp_obj_is_true(on);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(kora_esc_break_obj, kora_esc_break);

// size(): asked in raw mode (CSI 18 t), the mode then put back.
static mp_obj_t kora_size(void) {
    int was = kora_console_get_mode();
    if (kora_console_mode(CON_RAW) != 0) {
        return mp_const_none;
    }
    mp_hal_stdout_tx_str("\x1b[18t");
    char answer[32];  // ESC [ 8 ; rows ; cols t
    size_t n = 0;
    while (n < sizeof answer - 1) {
        int c = mp_hal_stdin_rx_chr();
        if (c == 3 || c == 4) {  // Ctrl+C, Ctrl+D: no answer
            break;
        }
        answer[n++] = (char)c;
        if (c == 't') {
            break;
        }
    }
    answer[n] = '\0';
    kora_console_mode(was);
    unsigned rows, cols;
    if (sscanf(answer, "\x1b[8;%u;%ut", &rows, &cols) != 2) {
        return mp_const_none;
    }
    mp_obj_t items[2] = {MP_OBJ_NEW_SMALL_INT(cols), MP_OBJ_NEW_SMALL_INT(rows)};
    return mp_obj_new_tuple(2, items);
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_size_obj, kora_size);

// repl(): MicroPython's own line editor and REPL, KoraCLI's fallback, until
// Ctrl+D (or sys.exit()).
static mp_obj_t kora_repl(void) {
    for (;;) {
        int ret = pyexec_mode_kind == PYEXEC_MODE_RAW_REPL ? pyexec_raw_repl()
                                                            : pyexec_friendly_repl();
        if (ret & PYEXEC_FORCED_EXIT) {
            pyexec_mode_kind = PYEXEC_MODE_FRIENDLY_REPL;
            return mp_const_none;
        }
    }
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_repl_obj, kora_repl);

// Running programs ---------------------------------------------------------

#define MAX_ARGV 16  // the kernel's MAX_ARGS

// A captured stream: the port the child writes to, and what came.
typedef struct {
    int port, bit;
    vstr_t text;
} capture_t;

static bool capture_open(capture_t *c) {
    c->bit = sig_alloc(-1);
    c->port = c->bit >= 0 ? port_create(NULL) : -1;
    if (c->port <= 0 || port_signal(c->port, c->bit) != 0) {
        if (c->port > 0) {
            port_delete(c->port);
        }
        if (c->bit >= 0) {
            sig_free(c->bit);
        }
        return false;
    }
    vstr_init(&c->text, 64);
    return true;
}

// Serve the packets on the port: writes are kept, the rest refused (the
// child's stdin stays the console, so it never reads here).
static void capture_serve(capture_t *c) {
    struct port_message got;
    while (port_get(c->port, &got) == 1) {
        if (got.kind != PORT_MESSAGE) {
            continue;
        }
        struct con_packet *packet = got.msg;
        if (packet->op == CON_WRITE) {
            vstr_add_strn(&c->text, packet->buf, packet->length);
            packet->result = (long)packet->length;
        } else {
            packet->result = -1;
        }
        port_reply(packet);
    }
}

static void capture_close(capture_t *c) {
    capture_serve(c);  // what is still there
    port_delete(c->port);
    sig_free(c->bit);
}

// execute(argv, stdin=None, stdout=None, append=False, core=-1,
//         background=False, capture=False): run the program argv[0] (a name
// from c:, or a path) with the arguments argv[1:].
//   - stdin, stdout: files to read from and write to (stdout appended to
//     with append) instead of the console;
//   - core: the core it runs on, -1 for the kernel's choice;
//   - background: start it and return its pid at once (reap() later);
//   - capture: its stdout and stderr kept instead of shown: the result is
//     (status, stdout, stderr), the texts as str;
//   - report: "[pid] exited with N" printed when it ends in front, "[pid]
//     started" when it goes to the background, as KoraOS's shells say.
// Otherwise it runs in front, with the console cooked for it, and the result
// is its exit status. Ctrl+C meanwhile is passed on to it as a break (which
// ends it unless it handles breaks), and ESC is a key again for it.
static mp_obj_t kora_execute(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    enum { ARG_argv, ARG_stdin, ARG_stdout, ARG_append, ARG_core, ARG_background, ARG_capture,
           ARG_report };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_argv, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_stdin, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_stdout, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_append, MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_core, MP_ARG_INT, {.u_int = -1} },
        { MP_QSTR_background, MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_capture, MP_ARG_BOOL, {.u_bool = false} },
        { MP_QSTR_report, MP_ARG_BOOL, {.u_bool = false} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, args, kwargs, MP_ARRAY_SIZE(allowed), allowed, vals);

    size_t argc;
    mp_obj_t *items;
    mp_obj_get_array(vals[ARG_argv].u_obj, &argc, &items);
    if (argc == 0 || argc > MAX_ARGV) {
        mp_raise_ValueError(MP_ERROR_TEXT("1 to 16 arguments"));
    }
    char *argv[MAX_ARGV + 1];
    for (size_t i = 0; i < argc; i++) {
        argv[i] = (char *)mp_obj_str_get_str(items[i]);
    }
    argv[argc] = NULL;
    mp_int_t core = vals[ARG_core].u_int;
    if (core < -1 || core > 3) {
        mp_raise_ValueError(MP_ERROR_TEXT("core 0 to 3"));
    }
    bool background = vals[ARG_background].u_bool;
    bool capturing = vals[ARG_capture].u_bool && !background;

    struct std_handle std[3];
    if (std_handles(std) != 0) {
        mp_raise_OSError(MP_EIO);
    }
    int in = -1, out = -1;
    if (vals[ARG_stdin].u_obj != mp_const_none) {
        in = open(mp_obj_str_get_str(vals[ARG_stdin].u_obj), O_RDONLY);
        if (in < 0) {
            mp_raise_OSError(MP_ENOENT);
        }
        std[0] = (struct std_handle){STD_FILE, in};
    }
    if (vals[ARG_stdout].u_obj != mp_const_none && !capturing) {
        out = open(mp_obj_str_get_str(vals[ARG_stdout].u_obj),
                   O_WRONLY | O_CREAT | (vals[ARG_append].u_bool ? O_APPEND : O_TRUNC));
        if (out < 0) {
            if (in >= 0) {
                close(in);
            }
            mp_raise_OSError(MP_EIO);
        }
        std[1] = (struct std_handle){STD_FILE, out};
    }
    capture_t cap[2];
    if (capturing) {
        if (!capture_open(&cap[0])) {
            goto no_ports;
        }
        if (!capture_open(&cap[1])) {
            capture_close(&cap[0]);
            vstr_clear(&cap[0].text);
            goto no_ports;
        }
        std[1] = (struct std_handle){STD_PORT, cap[0].port};
        std[2] = (struct std_handle){STD_PORT, cap[1].port};
    }

    int end_bit = background ? -1 : sig_alloc(-1);
    int flags = SPAWN_NOWAIT | (core >= 0 ? SPAWN_CORE(core) : 0) |
                (end_bit >= 0 ? SPAWN_SIGNAL(end_bit) : 0);
    int mode = kora_console_get_mode();
    bool esc = esc_on;
    if (!background) {
        kora_console_mode(CON_COOKED);  // the console as programs expect it
        if (esc) {
            console_esc_break(0);  // ESC is the program's key
        }
    }
    int pid = spawn_std(argv[0], (int)argc, argv, flags, std);
    if (in >= 0) {
        close(in);  // the child has its own copies
    }
    if (out >= 0) {
        close(out);
    }
    int code = 0;
    if (pid >= 0 && !background) {
        unsigned wait_mask = SIG_MASK(end_bit) | SIG_MASK(SIG_BREAK);
        if (capturing) {
            wait_mask |= SIG_MASK(cap[0].bit) | SIG_MASK(cap[1].bit);
        }
        for (;;) {
            unsigned got = sig_wait(wait_mask);
            if (capturing) {
                capture_serve(&cap[0]);
                capture_serve(&cap[1]);
            }
            if ((got & SIG_MASK(SIG_BREAK)) && sig_send(pid, SIG_MASK(SIG_BREAK)) != 0) {
                kill(pid);  // its main thread has ended: nobody takes a break
            }
            if (waitpid(pid, &code, WNOHANG) == pid) {
                break;
            }
        }
    }
    if (end_bit >= 0) {
        sig_free(end_bit);
    }
    if (!background) {
        if (esc) {
            console_esc_break(1);
        }
        kora_console_mode(mode);
    }
    mp_obj_t result;
    if (capturing) {
        capture_close(&cap[0]);
        capture_close(&cap[1]);
        mp_obj_t items3[3] = {MP_OBJ_NEW_SMALL_INT(code), mp_obj_new_str_from_vstr(&cap[0].text),
                              mp_obj_new_str_from_vstr(&cap[1].text)};
        result = pid >= 0 ? mp_obj_new_tuple(3, items3) : mp_const_none;
    } else {
        result = MP_OBJ_NEW_SMALL_INT(background ? pid : code);
    }
    if (pid < 0) {
        mp_raise_OSError(MP_ENOENT);  // no such program, or no such core
    }
    if (vals[ARG_report].u_bool) {
        printf(background ? "[%d] started\n" : "[%d] exited with %d\n", pid, code);
    }
    return result;

no_ports:
    if (in >= 0) {
        close(in);
    }
    if (out >= 0) {
        close(out);
    }
    mp_raise_OSError(MP_ENOMEM);
}
static MP_DEFINE_CONST_FUN_OBJ_KW(kora_execute_obj, 1, kora_execute);

// reap(): the background jobs that ended since, a (pid, status) each. A
// program run in front is reaped by execute() itself.
static mp_obj_t kora_reap(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    int code, pid;
    while ((pid = waitpid(-1, &code, WNOHANG)) > 0) {
        mp_obj_t items[2] = {MP_OBJ_NEW_SMALL_INT(pid), MP_OBJ_NEW_SMALL_INT(code)};
        mp_obj_list_append(list, mp_obj_new_tuple(2, items));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_reap_obj, kora_reap);

// kill(pid): end program `pid`. False if there is none.
static mp_obj_t kora_kill(mp_obj_t pid) {
    return mp_obj_new_bool(kill(mp_obj_get_int(pid)) == 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(kora_kill_obj, kora_kill);

// assign(name, target): `name:` is the existing directory `target` from now
// on; assign(name) removes it (sys: is fixed). OSError if refused.
static mp_obj_t kora_assign(size_t n_args, const mp_obj_t *args) {
    const char *target = n_args > 1 && args[1] != mp_const_none ? mp_obj_str_get_str(args[1]) : NULL;
    check(assign(mp_obj_str_get_str(args[0]), target) < 0 ? -1 : 0);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(kora_assign_obj, 1, 2, kora_assign);

static const mp_rom_map_elem_t kora_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__kora) },
    { MP_ROM_QSTR(MP_QSTR_execute), MP_ROM_PTR(&kora_execute_obj) },
    { MP_ROM_QSTR(MP_QSTR_reap), MP_ROM_PTR(&kora_reap_obj) },
    { MP_ROM_QSTR(MP_QSTR_kill), MP_ROM_PTR(&kora_kill_obj) },
    { MP_ROM_QSTR(MP_QSTR_assign), MP_ROM_PTR(&kora_assign_obj) },
    { MP_ROM_QSTR(MP_QSTR_uptime), MP_ROM_PTR(&kora_uptime_obj) },
    { MP_ROM_QSTR(MP_QSTR_volumes), MP_ROM_PTR(&kora_volumes_obj) },
    { MP_ROM_QSTR(MP_QSTR_assigns), MP_ROM_PTR(&kora_assigns_obj) },
    { MP_ROM_QSTR(MP_QSTR_key), MP_ROM_PTR(&kora_key_obj) },
    { MP_ROM_QSTR(MP_QSTR_mode), MP_ROM_PTR(&kora_mode_obj) },
    { MP_ROM_QSTR(MP_QSTR_bar), MP_ROM_PTR(&kora_bar_obj) },
    { MP_ROM_QSTR(MP_QSTR_esc_break), MP_ROM_PTR(&kora_esc_break_obj) },
    { MP_ROM_QSTR(MP_QSTR_size), MP_ROM_PTR(&kora_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_repl), MP_ROM_PTR(&kora_repl_obj) },
    { MP_ROM_QSTR(MP_QSTR_COOKED), MP_ROM_INT(CON_COOKED) },
    { MP_ROM_QSTR(MP_QSTR_RAW), MP_ROM_INT(CON_RAW) },
    { MP_ROM_QSTR(MP_QSTR_EVENTS), MP_ROM_INT(CON_EVENTS) },
    { MP_ROM_QSTR(MP_QSTR_TOP), MP_ROM_INT(CON_BAR_TOP) },
    { MP_ROM_QSTR(MP_QSTR_BOTTOM), MP_ROM_INT(CON_BAR_BOTTOM) },
};
static MP_DEFINE_CONST_DICT(kora_globals, kora_globals_table);

const mp_obj_module_t kora_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&kora_globals,
};
MP_REGISTER_MODULE(MP_QSTR__kora, kora_module);
