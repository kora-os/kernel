#include "lib/string.h"

// Clang's loop-idiom pass will not rewrite the byte loops below into calls to
// the very function being compiled, so these definitions are safe from
// self-recursion even though other code's loops compile down to calls here.
void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    if (d == s || n == 0) {
        return dest;
    }
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

void *memset(void *dest, int c, size_t n) {
    uint8_t *d = dest;
    for (size_t i = 0; i < n; i++) {
        d[i] = (uint8_t)c;
    }
    return dest;
}

int strlen(const char *str) {
    int len = 0;
    while (*str++) {
        len++;
    }
    return len;
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return (unsigned char)(*str1) - (unsigned char)(*str2);
}

int strncmp(const char *str1, const char *str2, size_t n) {
    while (n && (*str1 && (*str1 == *str2))) {
        str1++;
        str2++;
        n--;
    }
    return (unsigned char)(*str1) - (unsigned char)(*str2);
}

char *strcpy(char *dest, const char *src) {
    char *ret = dest;
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (n && (*src)) {
        *dest++ = *src++;
        n--;
    }
    if (n) {
        *dest = '\0';
    }
    return ret;
}

char *strcat(char *dest, const char *src) {
    char *ret = dest;
    while (*dest) {
        dest++;
    }
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return ret;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *ret = dest;
    while (*dest) {
        dest++;
    }
    while (n && (*src)) {
        *dest++ = *src++;
        n--;
    }
    if (n) {
        *dest = '\0';
    }
    return ret;
}
