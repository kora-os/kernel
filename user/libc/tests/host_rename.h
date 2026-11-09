// SPDX-License-Identifier: GPL-3.0-or-later
//
// Force-included (-include) when the libc sources and tests are built with the
// host compiler. Renames every <string.h> symbol so the code under test never
// collides with, or is silently replaced by, the host libc or the sanitizer's
// interceptors (which would make the tests pass against the wrong code).
#ifndef LIBC_HOST_RENAME_H
#define LIBC_HOST_RENAME_H

#define strcpy kora_strcpy
#define strcat kora_strcat
#define strlen kora_strlen
#define strncpy kora_strncpy
#define strncat kora_strncat
#define strcmp kora_strcmp
#define strncmp kora_strncmp
#define strstr kora_strstr
#define strrstr kora_strrstr
#define memcpy kora_memcpy
#define memset kora_memset
#define memmove kora_memmove
#define memcmp kora_memcmp

#endif // LIBC_HOST_RENAME_H
