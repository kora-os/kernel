// SPDX-License-Identifier: GPL-3.0-or-later
//
// The `os` module of MicroPython on KoraOS, over its volumes and assigns
// (libk): listdir, stat, mkdir, rmdir, remove, rename, getcwd, chdir and
// sync. Paths are KoraOS's (`sys:python`, `df0:notes`, relative ones). A
// failure raises OSError with the most likely errno: the kernel does not say
// why a call failed.
#include <string.h>

#include "libk/koraos.h"
#include "py/mperrno.h"
#include "py/objstr.h"
#include "py/runtime.h"

// A path argument; the current directory ("." or "") when there is none.
static const char *path_arg(size_t n_args, const mp_obj_t *args) {
    const char *path = n_args > 0 ? mp_obj_str_get_str(args[0]) : "";
    return path[0] != '\0' ? path : ".";
}

// The errno for a path that failed: none there, or something else.
static void raise_for(const char *path, int otherwise) {
    struct stat st;
    mp_raise_OSError(stat(path, &st) != 0 ? MP_ENOENT : otherwise);
}

// listdir(path="."): the names in a directory, the current one by default.
static mp_obj_t os_listdir(size_t n_args, const mp_obj_t *args) {
    const char *path = path_arg(n_args, args);
    struct stat st;
    if (stat(path, &st) != 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    if (!st.is_dir) {
        mp_raise_OSError(MP_ENOTDIR);
    }
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        mp_raise_OSError(MP_EIO);
    }
    mp_obj_t list = mp_obj_new_list(0, NULL);
    static struct dirent entry;  // 780 bytes: kept off the stack
    int got;
    while ((got = readdir(fd, &entry)) == 1) {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0) {
            continue;
        }
        mp_obj_list_append(list, mp_obj_new_str(entry.name, strlen(entry.name)));
    }
    close(fd);
    if (got < 0) {
        mp_raise_OSError(MP_EIO);
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(os_listdir_obj, 0, 1, os_listdir);

// stat(path): MicroPython's 10-tuple. KoraOS knows a file's size and whether
// it is a directory; the times and owners are 0.
static mp_obj_t os_stat(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    struct stat st;
    if (stat(path, &st) != 0) {
        mp_raise_OSError(MP_ENOENT);
    }
    mp_obj_tuple_t *t = MP_OBJ_TO_PTR(mp_obj_new_tuple(10, NULL));
    t->items[0] = MP_OBJ_NEW_SMALL_INT(st.is_dir ? 0x4000 : 0x8000);  // S_IFDIR, S_IFREG
    for (int i = 1; i < 10; i++) {
        t->items[i] = MP_OBJ_NEW_SMALL_INT(0);
    }
    t->items[6] = mp_obj_new_int_from_uint(st.size);
    return MP_OBJ_FROM_PTR(t);
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_stat_obj, os_stat);

static mp_obj_t os_mkdir(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    struct stat st;
    if (stat(path, &st) == 0) {
        mp_raise_OSError(MP_EEXIST);
    }
    if (mkdir(path) != 0) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_mkdir_obj, os_mkdir);

static mp_obj_t os_rmdir(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    if (rmdir(path) != 0) {
        raise_for(path, MP_EIO);  // not empty, not a directory, or read-only
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_rmdir_obj, os_rmdir);

static mp_obj_t os_remove(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    struct stat st;
    if (stat(path, &st) == 0 && st.is_dir) {
        mp_raise_OSError(MP_EISDIR);
    }
    if (unlink(path) != 0) {
        raise_for(path, MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_remove_obj, os_remove);

static mp_obj_t os_rename(mp_obj_t old_in, mp_obj_t new_in) {
    const char *old_path = mp_obj_str_get_str(old_in);
    const char *new_path = mp_obj_str_get_str(new_in);
    if (rename(old_path, new_path) != 0) {
        raise_for(old_path, MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(os_rename_obj, os_rename);

static mp_obj_t os_getcwd(void) {
    static char cwd[KORA_PATH_MAX];  // kept off the stack
    if (getcwd(cwd, sizeof cwd) < 0) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_obj_new_str(cwd, strlen(cwd));
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_getcwd_obj, os_getcwd);

static mp_obj_t os_chdir(mp_obj_t path_in) {
    const char *path = mp_obj_str_get_str(path_in);
    if (chdir(path) != 0) {
        raise_for(path, MP_ENOTDIR);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(os_chdir_obj, os_chdir);

// sync(): write what the volumes hold back to their media.
static mp_obj_t os_sync(void) {
    if (sync() != 0) {
        mp_raise_OSError(MP_EIO);
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(os_sync_obj, os_sync);

static const mp_rom_map_elem_t os_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_os) },
    { MP_ROM_QSTR(MP_QSTR_sep), MP_ROM_QSTR(MP_QSTR__slash_) },
    { MP_ROM_QSTR(MP_QSTR_listdir), MP_ROM_PTR(&os_listdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_stat), MP_ROM_PTR(&os_stat_obj) },
    { MP_ROM_QSTR(MP_QSTR_mkdir), MP_ROM_PTR(&os_mkdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_rmdir), MP_ROM_PTR(&os_rmdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_remove), MP_ROM_PTR(&os_remove_obj) },
    { MP_ROM_QSTR(MP_QSTR_rename), MP_ROM_PTR(&os_rename_obj) },
    { MP_ROM_QSTR(MP_QSTR_getcwd), MP_ROM_PTR(&os_getcwd_obj) },
    { MP_ROM_QSTR(MP_QSTR_chdir), MP_ROM_PTR(&os_chdir_obj) },
    { MP_ROM_QSTR(MP_QSTR_sync), MP_ROM_PTR(&os_sync_obj) },
};
static MP_DEFINE_CONST_DICT(os_globals, os_globals_table);

const mp_obj_module_t kora_os_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&os_globals,
};
MP_REGISTER_MODULE(MP_QSTR_os, kora_os_module);
