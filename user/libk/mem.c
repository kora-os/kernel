// SPDX-License-Identifier: GPL-3.0-or-later
//
// Memory primitives for freestanding user programs. Clang may turn copy and
// fill loops into calls to these even with -fno-builtin, so libk provides
// them. Byte loops: small and obviously correct; the libc port replaces them.

#include "koraos.h"

void *memset(void *dest, int c, size_t n) {
    volatile unsigned char *d = dest;  // volatile: keep this a loop, not a call to itself
    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t n) {
    volatile unsigned char *d = dest;
    const unsigned char *s = src;
    while (n-- > 0) {
        *d++ = *s++;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    volatile unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        while (n-- > 0) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n-- > 0) {
            *--d = *--s;
        }
    }
    return dest;
}
