// SPDX-License-Identifier: GPL-3.0-or-later
//
// The report of a failed assert() (<assert.h>). The message is assembled
// here and written with one fputs, so it stays whole while other cores
// print. Not through fprintf: a program that asserts should not have to link
// the printf engine for it. A message longer than the buffer is cut short.

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#define MESSAGE_SIZE 512

static char message[MESSAGE_SIZE];  // static: the main thread's stack is 4 KB

static size_t append(size_t at, const char *s) {
    while (*s != '\0' && at < MESSAGE_SIZE - 2) {  // room for the newline and the NUL
        message[at++] = *s++;
    }
    return at;
}

void __assert_fail(const char *expression, const char *file, int line, const char *function) {
    char digits[12];
    int n = 0;
    unsigned value = line > 0 ? (unsigned)line : 0;
    do {
        digits[sizeof digits - 2 - n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && n < (int)sizeof digits - 1);
    digits[sizeof digits - 1] = '\0';

    size_t at = append(0, "Assertion failed: ");
    at = append(at, expression);
    at = append(at, ", function ");
    at = append(at, function);
    at = append(at, ", file ");
    at = append(at, file);
    at = append(at, ", line ");
    at = append(at, digits + sizeof digits - 1 - n);
    message[at++] = '\n';
    message[at] = '\0';
    fputs(message, stderr);
    abort();
}
