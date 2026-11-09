// SPDX-License-Identifier: GPL-3.0-or-later
//
// Implementation of standard C string and memory manipulation functions.

#include <stddef.h>
#include <string.h>

/**
 * Copies the string pointed to by src, including the terminating null byte
 * ('\0'), to the buffer pointed to by dest. Returns a pointer to the
 * destination string dest.
 */
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d = *src)) {
        d++;
        src++;
    }
    return dest;
}

/**
 * Appends the string pointed to by src to the end of the string pointed to by
 * dest. The terminating null byte of dest is overwritten by the first character
 * of src. Returns a pointer to the destination string dest.
 */
char *strcat(char *dest, const char *src) {
    char *d = dest;

    while (*d)
        d++;

    while ((*src)) {
        *d = *src;
        d++;
        src++;
    }
    *d = '\0';
    return dest;
}

/**
 * Computes the length of the string s, excluding the terminating null byte
 * ('\0'). Returns the number of characters in the string.
 */
size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

/**
 * Copies at most n characters from the string pointed to by src to dest.
 * If the length of src is less than n, the remainder of dest will be padded
 * with null bytes. Returns a pointer to the destination string dest.
 */
char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d = *src)) {
        d++;
        src++;
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}

/**
 * Appends at most n characters from src to dest, followed by a null byte.
 * Dest must have sufficient space for the result, including the null byte.
 * Returns a pointer to the destination string dest.
 */
char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;

    while ((*d))
        d++;

    while (n && *src) {
        *d++ = *src++;
        n--;
    }
    *d = '\0';
    return dest;
}

/**
 * Compares the strings s1 and s2.
 * Returns an integer less than, equal to, or greater than zero if s1 is found,
 * respectively, to be less than, to match, or be greater than s2.
 */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * Compares the first n characters of the strings s1 and s2.
 * Returns an integer less than, equal to, or greater than zero if s1 is found,
 * respectively, to be less than, to match, or be greater than s2.
 */
int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

/**
 * Finds the first occurrence of the substring needle in the string haystack.
 * Returns a pointer to the beginning of the substring, or NULL if the substring
 * is not found.
 */
char *strstr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;

    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n)
            return (char *)haystack;
    }
    return NULL;
}

/**
 * Finds the last occurrence of the substring needle in the string haystack.
 * Returns a pointer to the beginning of the substring, or NULL if the substring
 * is not found.
 */
char *strrstr(const char *haystack, const char *needle) {
    if (!*needle)
        return (char *)haystack;

    char *result = NULL;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n)
            result = (char *)haystack;
    }
    return result;
}

void *memcpy(void *dest, const void *src, size_t n) {
    char *d = dest;
    const char *s = src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *dest, int c, size_t n) {
    unsigned char *p = dest;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    while (n--) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}