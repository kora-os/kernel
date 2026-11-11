#ifndef LIBC_STDLIB_H
#define LIBC_STDLIB_H

#include <stddef.h>

#include <_libc/cdefs.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff

typedef struct {
    int quot;
    int rem;
} div_t;

typedef struct {
    long int quot;
    long int rem;
} ldiv_t;

typedef struct {
    long long int quot;
    long long int rem;
} lldiv_t;

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
int rand(void);
void srand(unsigned int seed);

_LIBC_NORETURN void abort(void);
int atexit(void (*func)(void));
int at_quick_exit(void (*func)(void));
_LIBC_NORETURN void exit(int status);
_LIBC_NORETURN void _Exit(int status);
_LIBC_NORETURN void quick_exit(int status);
// No environment is passed to KoraOS programs yet: always NULL.
char *getenv(const char *name);

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

int abs(int j);
long int labs(long int j);
long long int llabs(long long int j);
div_t div(int numer, int denom);
ldiv_t ldiv(long int numer, long int denom);
lldiv_t lldiv(long long int numer, long long int denom);

#endif // LIBC_STDLIB_H