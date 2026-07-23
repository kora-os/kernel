#pragma once

#include "common.h"

// Registry of user programs compiled as standalone PIE ELFs and embedded into
// the kernel image as read-only blobs (see the generated user_blobs.S and the
// user/ tree). With no filesystem yet, this table is how the kernel finds a
// program by name -- e.g. for the eventual spawn("hello") syscall and the ELF
// loader in the next step.
typedef struct user_program {
    const char *name;
    const unsigned char *start;  // first byte of the embedded ELF image
    const unsigned char *end;    // one past the last byte
} user_program_t;

// Number of embedded programs.
size_t user_program_count(void);

// Program at index i (0..count-1), or NULL if out of range.
const user_program_t *user_program_at(size_t i);

// Look up a program by name, or NULL if there is none.
const user_program_t *user_program_find(const char *name);

// Size in bytes of an embedded program's ELF image.
size_t user_program_size(const user_program_t *p);
