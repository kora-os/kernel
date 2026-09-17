// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small C-runtime pieces the vendored Circle code expects from its environment
// that KoraOS does not otherwise provide: malloc/free (routed to the kernel's
// operator new/delete) and a few string helpers used by option parsing. Circle's
// DebugHexDump is stubbed out.

#include <circle/debug.h>

using size_t = __SIZE_TYPE__;

extern "C" {

void *malloc(size_t size) {
    return ::operator new(size);
}

void free(void *ptr) {
    ::operator delete(ptr);
}

char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c) {
            return (char *)s;  // also returns the terminator when c == '\0'
        }
        if (*s == '\0') {
            return (char *)0;
        }
    }
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    unsigned long result = 0;
    for (;; s++) {
        int digit;
        if (*s >= '0' && *s <= '9') {
            digit = *s - '0';
        } else if (*s >= 'a' && *s <= 'z') {
            digit = *s - 'a' + 10;
        } else if (*s >= 'A' && *s <= 'Z') {
            digit = *s - 'A' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * (unsigned long)base + (unsigned long)digit;
    }
    if (endptr != (char **)0) {
        *endptr = (char *)s;
    }
    return result;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (str == (char *)0) {
        str = *saveptr;
    }
    // Skip leading delimiters.
    for (; *str != '\0'; str++) {
        const char *d = delim;
        bool is_delim = false;
        for (; *d != '\0'; d++) {
            if (*str == *d) {
                is_delim = true;
                break;
            }
        }
        if (!is_delim) {
            break;
        }
    }
    if (*str == '\0') {
        *saveptr = str;
        return (char *)0;
    }
    char *token = str;
    for (; *str != '\0'; str++) {
        const char *d = delim;
        for (; *d != '\0'; d++) {
            if (*str == *d) {
                *str = '\0';
                *saveptr = str + 1;
                return token;
            }
        }
    }
    *saveptr = str;
    return token;
}

}  // extern "C"

void DebugHexDump(const void *pStart, unsigned nBytes, const char *pSource,
                  unsigned nFlags) {
    (void)pStart;
    (void)nBytes;
    (void)pSource;
    (void)nFlags;
}
