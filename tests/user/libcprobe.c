// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for the libc on the real target: what the host unit
// tests cannot reach. The AArch64 binary128 strtold path, floating point in
// EL0 through libc code, errno, qsort/bsearch on the 4 KB main stack, the
// rand sequence (the same on every platform), snprintf with the AAPCS64
// va_list (the host tests run on other variadic conventions), and an atexit
// handler run by crt0's exit() after main returns. stdout is made fully
// buffered, so "libcprobe: checks passed" (printf) and then the handler's
// "libcprobe: atexit ran" (puts) only appear if exit() flushes stdout after
// running the handlers. A failed check prints itself (unbuffered) and the
// program exits non-zero.
#include "../../user/libk/koraos.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static int failed;

static void check(int ok, const char *what) {
    if (!ok) {
        kputs("libcprobe: FAILED ");
        kputs(what);
        kputs("\n");
        failed = 1;
    }
}

static void test_integers(void) {
    char *end;
    errno = 0;
    check(strtol("-9223372036854775808", NULL, 10) == LONG_MIN && errno == 0, "strtol LONG_MIN");
    check(strtol("9223372036854775808", &end, 10) == LONG_MAX && errno == ERANGE && *end == '\0',
          "strtol overflow sets ERANGE");
    errno = 0;
    check(strtoul("0x1f", NULL, 0) == 31 && atoi("  -42") == -42, "strtoul, atoi");
    check(abs(INT_MIN) == INT_MIN && labs(-7) == 7, "abs");
    div_t d = div(-7, 2);
    check(d.quot == -3 && d.rem == -1, "div");
    check(strcmp(strerror(ENOENT), "No such file or directory") == 0, "strerror");
}

static void test_floats(void) {
    char *end;
    volatile double x = strtod("2.5", &end);  // volatile: computed at run time
    check(x * 2 == 5.0 && *end == '\0', "strtod in EL0");
    check(strtof("0.25", NULL) == 0.25f, "strtof");
    check(atof("-1.5") < -1.4 && atof("-1.5") > -1.6, "atof");

    // long double is binary128 here: strtold widens by bits, no compiler-rt.
    long double wide = strtold("1.5", NULL);
    uint64_t words[2];
    memcpy(words, &wide, sizeof words);
    check(LDBL_MANT_DIG == 113 && sizeof wide == 16, "long double is binary128");
    check(words[1] == 0x3fff800000000000ull && words[0] == 0, "strtold(1.5) bits");
    wide = strtold("0.1", NULL);
    memcpy(words, &wide, sizeof words);
    check(words[1] == 0x3ffb999999999999ull && words[0] == 0xa000000000000000ull,
          "strtold(0.1) bits");
}

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

static int numbers[500];

static void test_sort(void) {
    srand(1);
    check(rand() == 908834774 && rand() == 1093944153, "rand sequence after srand(1)");
    for (int i = 0; i < 500; i++) {
        numbers[i] = rand() % 1000 - 500;
    }
    qsort(numbers, 500, sizeof numbers[0], cmp_int);
    int sorted = 1;
    for (int i = 1; i < 500; i++) {
        sorted &= numbers[i - 1] <= numbers[i];
    }
    check(sorted, "qsort");
    int key = numbers[123];
    int *hit = bsearch(&key, numbers, 500, sizeof numbers[0], cmp_int);
    check(hit != NULL && *hit == key, "bsearch");
    key = 10000;
    check(bsearch(&key, numbers, 500, sizeof numbers[0], cmp_int) == NULL, "bsearch miss");
}

static void test_heap(void) {
    char *p = malloc(100);
    char *q = calloc(10, 10);
    check(p != NULL && q != NULL && ((uintptr_t)p & 15) == 0, "malloc alignment");
    memset(p, 'x', 100);
    p = realloc(p, 5000);
    check(p != NULL && p[0] == 'x' && p[99] == 'x' && q[99] == 0, "realloc keeps data");
    free(p);
    free(q);
    check(getenv("PATH") == NULL, "getenv");
}

static void test_snprintf(void) {
    char buf[160];
    // More integer arguments than the eight argument registers, with doubles
    // (which travel in FP registers) in between, so va_arg walks both save
    // areas and then the stack.
    int n = snprintf(buf, sizeof buf, "%d %ld %s %c %x %llu %p %hhd %f %d %zu %e %-4d|%05d %s",
                     -1, 123456789012L, "str", 'c', 0xbeefu, 18446744073709551615ull,
                     (void *)0x1000, 300, 1.0, 42, (size_t)7, 2.0, 9, -12, "end");
    const char *want = "-1 123456789012 str c beef 18446744073709551615 0x1000 44 1.000000 42 7 "
                       "2.000000e+00 9   |-0012 end";
    check(n == (int)strlen(want) && strcmp(buf, want) == 0, "snprintf with the AAPCS64 va_list");
    check(snprintf(NULL, 0, "%8.3s|%+d", "abcdef", 5) == 11, "snprintf measures");

    // Floats: exact decimal output, and %L reading a binary128 argument
    // (narrowed by hand: there is no compiler-rt).
    n = snprintf(buf, sizeof buf, "%.3f|%e|%g|%a|%.0f|%.2f|%Lf|%Lg|%G", 3.14159, 1e-300, 0.0001,
                 1.0, 2.5, 0.125, (long double)1.5, strtold("1e100", NULL), 1e100);
    want = "3.142|1.000000e-300|0.0001|0x1p+0|2|0.12|1.500000|1e+100|1E+100";
    check(n == (int)strlen(want) && strcmp(buf, want) == 0, "snprintf floats");
    snprintf(buf, sizeof buf, "%.17g", 0.1);
    check(strcmp(buf, "0.10000000000000001") == 0, "%.17g of 0.1");
}

static void test_strtod(void) {
    char *end;
    check(strtod("0x1.8p1", &end) == 3.0 && *end == '\0', "strtod hex");
    check(strtod("2.2250738585072011e-308", NULL) == 0x0.fffffffffffffp-1022,
          "strtod near the subnormal boundary");
    check(strtod("9007199254740993", NULL) == 9007199254740992.0, "strtod ties to even");
    check(strtof("3.4028235e38", NULL) == 0x1.fffffep127f, "strtof FLT_MAX");
    errno = 0;
    check(strtod("1e400", NULL) > DBL_MAX && errno == ERANGE, "strtod overflow");
    errno = 0;
}

static void at_exit_handler(void) {
    puts("libcprobe: atexit ran");
}

int main(void) {
    check(setvbuf(stdout, NULL, _IOFBF, 0) == 0, "setvbuf");
    test_integers();
    test_floats();
    test_sort();
    test_heap();
    test_snprintf();
    test_strtod();
    if (failed) {
        return 1;
    }
    check(atexit(at_exit_handler) == 0, "atexit");
    printf("libcprobe: %s\n", "checks passed");
    return 0;  // crt0 passes this to exit(): the handler runs, then stdout is flushed
}
