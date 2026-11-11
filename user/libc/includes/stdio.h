// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdio.h> (C11 7.21): files and streams, character and block I/O,
// formatted output, positioning, remove and rename. stdout is line buffered,
// stderr unbuffered, stdin reads one byte at a time (the console is
// interactive and raw: no echo, Enter is '\r'); exit() flushes and closes
// everything. Not provided: the scanf family, tmpfile and tmpnam. The
// va_list parameters are spelled __builtin_va_list so this header does not
// define va_list itself, as the standard asks.
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

// 16 files per program besides the console's three.
#define FOPEN_MAX 19
// Longest qualified path, NUL included (the kernel's KORA_PATH_MAX).
#define FILENAME_MAX 4128

typedef struct __libc_file FILE;
typedef long fpos_t;

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

int remove(const char *filename);
int rename(const char *old, const char *new);

int fclose(FILE *stream);
int fflush(FILE *stream);
FILE *fopen(const char *restrict filename, const char *restrict mode);
FILE *freopen(const char *restrict filename, const char *restrict mode, FILE *restrict stream);
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

int fgetc(FILE *stream);
char *fgets(char *restrict s, int n, FILE *restrict stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int c, FILE *stream);
int fputc(int c, FILE *stream);
int fputs(const char *restrict s, FILE *restrict stream);
int putc(int c, FILE *stream);
int putchar(int c);
int puts(const char *s);
size_t fread(void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream);
size_t fwrite(const void *restrict ptr, size_t size, size_t nmemb, FILE *restrict stream);

int fgetpos(FILE *restrict stream, fpos_t *restrict pos);
int fseek(FILE *stream, long int offset, int whence);
int fsetpos(FILE *stream, const fpos_t *pos);
long int ftell(FILE *stream);
void rewind(FILE *stream);

void clearerr(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

#endif // LIBC_STDIO_H
