#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#include <stddef.h>

#include <_libc/cdefs.h>

double atof(const char *nptr);
int atoi(const char *nptr);
long int atol(const char *nptr);
long long int atoll(const char *nptr);

/*
We need snprintf for these

int strfromd(char *restrict s, size_t n, const char *restrict format, double fp);
int strfromf(char *restrict s, size_t n, const char *restrict format, float fp);
int strfroml(char *restrict s, size_t n, const char *restrict format, long double fp);
*/

double strtod(const char *restrict nptr, char **restrict endptr);
float strtof(const char *restrict nptr, char **restrict endptr);
long double strtold(const char *restrict nptr, char **restrict endptr);
long int strtol(const char *restrict nptr, char **restrict endptr, int base);
long long int strtoll(const char *restrict nptr, char **restrict endptr, int base);
unsigned long int strtoul(const char *restrict nptr, char **restrict endptr, int base);
unsigned long long int strtoull(const char *restrict nptr,
                                char **restrict endptr, int base);

void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *malloc(size_t size);
void *realloc(void *ptr, size_t size);
_LIBC_NORETURN void abort(void);
_LIBC_NORETURN void exit(int status);
_LIBC_NORETURN void _Exit(int status);

#endif // LIBC_STDLIB_H