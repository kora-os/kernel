// SPDX-License-Identifier: GPL-3.0-or-later
//
// The rest of <string.h> (C11 7.24.5, 7.24.4.3, 7.24.4.5): memchr, strchr,
// strrchr, strspn, strcspn, strpbrk, strtok, and the collation functions,
// which in the only locale ("C") compare and copy bytes.

#include <string.h>

void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    unsigned char byte = (unsigned char)c;
    for (size_t i = 0; i < n; i++) {
        if (p[i] == byte) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

// The terminating NUL counts as part of the string: strchr(s, 0) finds it.
char *strchr(const char *s, int c) {
    char ch = (char)c;
    for (;; s++) {
        if (*s == ch) {
            return (char *)s;
        }
        if (*s == '\0') {
            return NULL;
        }
    }
}

char *strrchr(const char *s, int c) {
    char ch = (char)c;
    const char *last = NULL;
    for (;; s++) {
        if (*s == ch) {
            last = s;
        }
        if (*s == '\0') {
            return (char *)last;
        }
    }
}

// A 256-bit set of the bytes in `chars`.
static void byte_set(unsigned long set[4], const char *chars) {
    set[0] = set[1] = set[2] = set[3] = 0;
    for (const unsigned char *p = (const unsigned char *)chars; *p; p++) {
        set[*p / 64] |= 1ul << (*p % 64);
    }
}

static int in_set(const unsigned long set[4], unsigned char c) {
    return (set[c / 64] >> (c % 64)) & 1;
}

size_t strspn(const char *s, const char *accept) {
    unsigned long set[4];
    byte_set(set, accept);
    size_t n = 0;
    while (s[n] != '\0' && in_set(set, (unsigned char)s[n])) {
        n++;
    }
    return n;
}

size_t strcspn(const char *s, const char *reject) {
    unsigned long set[4];
    byte_set(set, reject);
    size_t n = 0;
    while (s[n] != '\0' && !in_set(set, (unsigned char)s[n])) {
        n++;
    }
    return n;
}

char *strpbrk(const char *s, const char *accept) {
    s += strcspn(s, accept);
    return *s != '\0' ? (char *)s : NULL;
}

// One saved position for the whole program, as the standard specifies; not
// safe across threads.
static char *strtok_next;

char *strtok(char *restrict s, const char *restrict delim) {
    if (s == NULL) {
        s = strtok_next;
        if (s == NULL) {
            return NULL;
        }
    }
    s += strspn(s, delim);
    if (*s == '\0') {
        strtok_next = NULL;
        return NULL;
    }
    char *token = s;
    s += strcspn(s, delim);
    if (*s != '\0') {
        *s++ = '\0';
        strtok_next = s;
    } else {
        strtok_next = NULL;
    }
    return token;
}

int strcoll(const char *s1, const char *s2) {
    return strcmp(s1, s2);
}

size_t strxfrm(char *restrict dest, const char *restrict src, size_t n) {
    size_t len = strlen(src);
    if (n > 0) {
        size_t k = len < n - 1 ? len : n - 1;
        memcpy(dest, src, k);
        dest[k] = '\0';
    }
    return len;
}
