// SPDX-License-Identifier: GPL-3.0-or-later
//
// Force-included (-include) when the libc sources and tests are built with the
// host compiler. Renames libc symbols so the code under test never collides
// with, or is silently replaced by, the host libc or the sanitizer's
// interceptors (which would make the tests pass against the wrong code).
#ifndef LIBC_HOST_RENAME_H
#define LIBC_HOST_RENAME_H

#define isalnum kora_isalnum
#define isalpha kora_isalpha
#define islower kora_islower
#define isupper kora_isupper
#define isdigit kora_isdigit
#define isxdigit kora_isxdigit
#define iscntrl kora_iscntrl
#define isgraph kora_isgraph
#define isspace kora_isspace
#define isblank kora_isblank
#define isprint kora_isprint
#define ispunct kora_ispunct
#define tolower kora_tolower
#define toupper kora_toupper

#define atof kora_atof
#define atoi kora_atoi
#define atol kora_atol
#define atoll kora_atoll
#define strfromd kora_strfromd
#define strfromf kora_strfromf
#define strfroml kora_strfroml
#define strtod kora_strtod
#define strtof kora_strtof
#define strtold kora_strtold
#define strtol kora_strtol
#define strtoll kora_strtoll
#define strtoul kora_strtoul
#define strtoull kora_strtoull

#define malloc kora_malloc
#define free kora_free
#define calloc kora_calloc
#define realloc kora_realloc
#define abort kora_abort
#define exit kora_exit
#define _Exit kora_Exit
#define atexit kora_atexit
#define at_quick_exit kora_at_quick_exit
#define quick_exit kora_quick_exit
#define getenv kora_getenv
#define rand kora_rand
#define srand kora_srand
#define qsort kora_qsort
#define bsearch kora_bsearch
#define abs kora_abs
#define labs kora_labs
#define llabs kora_llabs
#define div kora_div
#define ldiv kora_ldiv
#define lldiv kora_lldiv

// The libc's syscall entry; tests link tests/fake_syscall.c in its place.
#define __syscall kora_syscall

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
#define strerror kora_strerror

#define snprintf kora_snprintf
#define sprintf kora_sprintf
#define vsnprintf kora_vsnprintf
#define vsprintf kora_vsprintf

#define __errno_location kora_errno_location

#endif // LIBC_HOST_RENAME_H
