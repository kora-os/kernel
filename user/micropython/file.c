// SPDX-License-Identifier: GPL-3.0-or-later
//
// Files for MicroPython on KoraOS: open() and its file objects over the
// libc's streams, and the source of modules for `import` and of scripts. Paths
// are KoraOS's: relative to the current directory, or qualified with a volume
// or an assign (`sys:python/x.py`, `df0:notes.txt`).
//
// A file object is FileIO (binary, bytes) or TextIOWrapper (text, str), as
// MicroPython's own: read, readinto, readline, readlines, write, seek, tell,
// flush, close, with-blocks and iteration by line. One nobody closed is
// closed when the GC collects it.
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "libk/koraos.h"
#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"

typedef struct {
    mp_obj_base_t base;
    FILE *f;
} kora_file_t;

static void check_open(kora_file_t *self) {
    if (self->f == NULL) {
        mp_raise_ValueError(MP_ERROR_TEXT("I/O operation on closed file"));
    }
}

static mp_uint_t file_read(mp_obj_t self_in, void *buf, mp_uint_t size, int *errcode) {
    kora_file_t *self = MP_OBJ_TO_PTR(self_in);
    check_open(self);
    size_t n = fread(buf, 1, size, self->f);
    if (n == 0 && ferror(self->f)) {
        clearerr(self->f);
        *errcode = MP_EIO;
        return MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t file_write(mp_obj_t self_in, const void *buf, mp_uint_t size, int *errcode) {
    kora_file_t *self = MP_OBJ_TO_PTR(self_in);
    check_open(self);
    size_t n = fwrite(buf, 1, size, self->f);
    if (n < size && ferror(self->f)) {
        clearerr(self->f);
        *errcode = MP_EIO;
        return n > 0 ? n : MP_STREAM_ERROR;
    }
    return n;
}

static mp_uint_t file_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    kora_file_t *self = MP_OBJ_TO_PTR(self_in);
    if (request == MP_STREAM_CLOSE) {
        if (self->f != NULL) {
            int failed = fclose(self->f);
            self->f = NULL;
            if (failed) {
                *errcode = MP_EIO;
                return MP_STREAM_ERROR;
            }
        }
        return 0;
    }
    check_open(self);
    switch (request) {
    case MP_STREAM_FLUSH:
        if (fflush(self->f) != 0) {
            *errcode = MP_EIO;
            return MP_STREAM_ERROR;
        }
        return 0;
    case MP_STREAM_SEEK: {
        struct mp_stream_seek_t *s = (struct mp_stream_seek_t *)arg;
        if (fseek(self->f, (long)s->offset, s->whence) != 0) {
            *errcode = MP_EINVAL;
            return MP_STREAM_ERROR;
        }
        s->offset = ftell(self->f);
        return 0;
    }
    default:
        *errcode = MP_EINVAL;
        return MP_STREAM_ERROR;
    }
}

static void file_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    (void)kind;
    kora_file_t *self = MP_OBJ_TO_PTR(self_in);
    mp_printf(print, "<io.%s %s>", mp_obj_get_type_str(self_in), self->f ? "open" : "closed");
}

static const mp_rom_map_elem_t file_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj) },
    { MP_ROM_QSTR(MP_QSTR_readlines), MP_ROM_PTR(&mp_stream_unbuffered_readlines_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_seek), MP_ROM_PTR(&mp_stream_seek_obj) },
    { MP_ROM_QSTR(MP_QSTR_tell), MP_ROM_PTR(&mp_stream_tell_obj) },
    { MP_ROM_QSTR(MP_QSTR_flush), MP_ROM_PTR(&mp_stream_flush_obj) },
    { MP_ROM_QSTR(MP_QSTR_close), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mp_stream_close_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&mp_identity_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&mp_stream___exit___obj) },
};
static MP_DEFINE_CONST_DICT(file_locals_dict, file_locals_dict_table);

static const mp_stream_p_t fileio_stream_p = {
    .read = file_read,
    .write = file_write,
    .ioctl = file_ioctl,
};

static const mp_stream_p_t textio_stream_p = {
    .read = file_read,
    .write = file_write,
    .ioctl = file_ioctl,
    .is_text = true,
};

MP_DEFINE_CONST_OBJ_TYPE(
    kora_type_fileio,
    MP_QSTR_FileIO,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, file_print,
    protocol, &fileio_stream_p,
    locals_dict, &file_locals_dict
    );

MP_DEFINE_CONST_OBJ_TYPE(
    kora_type_textio,
    MP_QSTR_TextIOWrapper,
    MP_TYPE_FLAG_ITER_IS_STREAM,
    print, file_print,
    protocol, &textio_stream_p,
    locals_dict, &file_locals_dict
    );

// open(path, mode="r"): r, w, a or x, with b for binary and + for both ways.
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    enum { ARG_file, ARG_mode };
    static const mp_arg_t allowed[] = {
        { MP_QSTR_file, MP_ARG_OBJ | MP_ARG_REQUIRED, {.u_rom_obj = MP_ROM_NONE} },
        { MP_QSTR_mode, MP_ARG_OBJ, {.u_rom_obj = MP_ROM_QSTR(MP_QSTR_r)} },
    };
    mp_arg_val_t vals[MP_ARRAY_SIZE(allowed)];
    mp_arg_parse_all(n_args, args, kwargs, MP_ARRAY_SIZE(allowed), allowed, vals);
    const char *path = mp_obj_str_get_str(vals[ARG_file].u_obj);
    const char *mode = mp_obj_str_get_str(vals[ARG_mode].u_obj);

    char how = 0;
    bool binary = false, update = false;
    for (const char *m = mode; *m; m++) {
        switch (*m) {
        case 'r': case 'w': case 'a': case 'x':
            if (how) {
                goto bad_mode;
            }
            how = *m;
            break;
        case 'b': binary = true; break;
        case 't': break;
        case '+': update = true; break;
        default: goto bad_mode;
        }
    }
    if (!how) {
        goto bad_mode;
    }
    if (how == 'x') {
        // Exclusive creation: no file there yet, then as 'w'.
        struct stat st;
        if (stat(path, &st) == 0) {
            mp_raise_OSError(MP_EEXIST);
        }
        how = 'w';
    }
    char cmode[4] = {how, 0, 0, 0};
    size_t i = 1;
    if (update) {
        cmode[i++] = '+';
    }
    cmode[i] = 'b';

    struct stat st;
    if (stat(path, &st) == 0 && st.is_dir) {
        mp_raise_OSError(MP_EISDIR);
    }
    FILE *f = fopen(path, cmode);
    if (f == NULL) {
        mp_raise_OSError(how == 'r' ? MP_ENOENT : MP_EIO);
    }
    kora_file_t *o = mp_obj_malloc_with_finaliser(kora_file_t,
        binary ? &kora_type_fileio : &kora_type_textio);
    o->f = f;
    return MP_OBJ_FROM_PTR(o);

bad_mode:
    mp_raise_ValueError(MP_ERROR_TEXT("invalid mode"));
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

// A module or a script: the whole file, which the lexer frees when done.
mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    const char *path = qstr_str(filename);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        mp_raise_OSError(MP_ENOENT);
    }
    struct stat st;
    size_t size = stat(path, &st) == 0 ? (size_t)st.size : 0;
    char *text = m_new(char, size + 1);
    size_t got = fread(text, 1, size, f);
    fclose(f);
    if (got != size) {
        m_del(char, text, size + 1);
        mp_raise_OSError(MP_EIO);
    }
    return mp_lexer_new_from_str_len(filename, text, got, size + 1);
}

mp_import_stat_t mp_import_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return MP_IMPORT_STAT_NO_EXIST;
    }
    return st.is_dir ? MP_IMPORT_STAT_DIR : MP_IMPORT_STAT_FILE;
}
