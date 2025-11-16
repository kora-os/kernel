#include "lib/string.h"

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
