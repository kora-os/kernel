#include "user/embedded.h"
#include "lib/string.h"

// Symbols emitted by the generated user_blobs.S, which .incbin's each user
// program's PIE ELF into the kernel's read-only data.
extern const unsigned char _user_hello_start[];
extern const unsigned char _user_hello_end[];
extern const unsigned char _user_init_start[];
extern const unsigned char _user_init_end[];
extern const unsigned char _user_shell_start[];
extern const unsigned char _user_shell_end[];
extern const unsigned char _user_echo_start[];
extern const unsigned char _user_echo_end[];
extern const unsigned char _user_gfxdemo_start[];
extern const unsigned char _user_gfxdemo_end[];

static const user_program_t programs[] = {
    {"hello", _user_hello_start, _user_hello_end},
    {"init", _user_init_start, _user_init_end},
    {"shell", _user_shell_start, _user_shell_end},
    {"echo", _user_echo_start, _user_echo_end},
    {"gfxdemo", _user_gfxdemo_start, _user_gfxdemo_end},
};

size_t user_program_count(void) {
    return sizeof(programs) / sizeof(programs[0]);
}

const user_program_t *user_program_at(size_t i) {
    if (i >= user_program_count()) {
        return NULL;
    }
    return &programs[i];
}

const user_program_t *user_program_find(const char *name) {
    for (size_t i = 0; i < user_program_count(); i++) {
        if (strcmp(programs[i].name, name) == 0) {
            return &programs[i];
        }
    }
    return NULL;
}

size_t user_program_size(const user_program_t *p) {
    return (size_t)(p->end - p->start);
}
