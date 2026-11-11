// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdio.h> (C11 7.21): formatted output to strings and to streams, and
// stream output. stdout is line buffered, stderr unbuffered; exit() flushes
// everything. Opening files and reading come next (see
// project_doc/libc-plan.md). The va_list parameters are spelled
// __builtin_va_list so this header does not define va_list itself, as the
// standard asks.
#ifndef LIBC_STDIO_H
#define LIBC_STDIO_H

#include <stddef.h>

#include <_libc/cdefs.h>

#define EOF (-1)
#define BUFSIZ 1024

// setvbuf modes.
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct __libc_file FILE;

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
// The standard asks for macros.
#ifndef stdin
#define stdin stdin
#endif
#ifndef stdout
#define stdout stdout
#endif
#ifndef stderr
#define stderr stderr
#endif

int fflush(FILE *stream);
int setvbuf(FILE *restrict stream, char *restrict buf, int mode, size_t size);
void setbuf(FILE *restrict stream, char *restrict buf);

int fprintf(FILE *restrict stream, const char *restrict format, ...) _LIBC_PRINTF(2, 3);
int printf(const char *restrict format, ...) _LIBC_PRINTF(1, 2);
int snprintf(char *restrict s, size_t n, const char *restrict format, ...) _LIBC_PRINTF(3, 4);
int sprintf(char *restrict s, const char *restrict format, ...) _LIBC_PRINTF(2, 3);
int vfprintf(FILE *restrict stream, const char *restrict format, __builtin_va_list arg)
    _LIBC_PRINTF(2, 0);
int vprintf(const char *restrict format, __builtin_va_list arg) _LIBC_PRINTF(1, 0);
int vsnprintf(char *restrict s, size_t n, const char *restrict format, __builtin_va_list arg)
    _LIBC_PRINTF(3, 0);
int vsprintf(char *restrict s, const char *restrict format, __builtin_va_list arg)
    _LIBC_PRINTF(2, 0);

int fputc(int c, FILE *stream);
int fputs(const char *restrict s, FILE *restrict stream);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
size_t fwrite(const void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

#endif // LIBC_STDIO_H
