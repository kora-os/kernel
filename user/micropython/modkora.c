// SPDX-License-Identifier: GPL-3.0-or-later
//
// The `kora` module: KoraOS from Python.
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
//
// The console parts are KoraCLI's (step 1b, on step 1a's console). Running
// commands (kora.run, kora.capture) is step 1c.
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

// assigns(): a (name, target) tuple for each assign, as `assign` lists them.
static mp_obj_t kora_assigns(void) {
    mp_obj_t list = mp_obj_new_list(0, NULL);
    static struct assign_info a;  // 4 KB of target: kept off the stack
    for (unsigned i = 0; assign_info(i, &a) == 1; i++) {
        mp_obj_t items[2] = {
            field_str(a.name, sizeof a.name),
            field_str(a.target, sizeof a.target),
        };
        mp_obj_list_append(list, mp_obj_new_tuple(2, items));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(kora_assigns_obj, kora_assigns);

static void check(int result) {
    if (result != 0) {
        mp_raise_OSError(MP_EINVAL);
    }
}

// key(): the next key press, waiting for it.
static mp_obj_t kora_key(void) {
    struct con_key_event event;
    int got = kora_read_key(&event);
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
static MP_DEFINE_CONST_FUN_OBJ_0(kora_key_obj, kora_key);

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

static mp_obj_t kora_esc_break(mp_obj_t on) {
    check(console_esc_break(mp_obj_is_true(on)));
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

static const mp_rom_map_elem_t kora_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_kora) },
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
MP_REGISTER_MODULE(MP_QSTR_kora, kora_module);
