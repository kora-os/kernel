// SPDX-License-Identifier: GPL-3.0-or-later
//
// The `kora` module: KoraOS from Python. For now (KoraCLI step 0b) what needs
// no new design: uptime(), volumes() and assigns(). Running commands
// (kora.run, kora.capture) is step 1c.
#include <string.h>
#include <time.h>

#include "libk/koraos.h"
#include "py/runtime.h"

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

static const mp_rom_map_elem_t kora_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_kora) },
    { MP_ROM_QSTR(MP_QSTR_uptime), MP_ROM_PTR(&kora_uptime_obj) },
    { MP_ROM_QSTR(MP_QSTR_volumes), MP_ROM_PTR(&kora_volumes_obj) },
    { MP_ROM_QSTR(MP_QSTR_assigns), MP_ROM_PTR(&kora_assigns_obj) },
};
static MP_DEFINE_CONST_DICT(kora_globals, kora_globals_table);

const mp_obj_module_t kora_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&kora_globals,
};
MP_REGISTER_MODULE(MP_QSTR_kora, kora_module);
