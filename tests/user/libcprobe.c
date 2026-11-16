// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for the libc on the real target: what the host unit
// tests cannot reach. The AArch64 binary128 strtold path, floating point in
// EL0 through libc code, errno, qsort/bsearch on the 4 KB main stack, the
// rand sequence (the same on every platform), snprintf with the AAPCS64
// va_list (the host tests run on other variadic conventions), setjmp and
// longjmp (AArch64 assembly, which CI's x86-64 hosts cannot run) with a
// signal handler that jumps out of abort(), <math.h> (the functions that are
// one AArch64 instruction, musl's functions giving the same bits as on the
// hosts, and the long double functions, which move binary128 bits by hand),
// and an atexit
// handler run by crt0's exit() after main returns. stdout is made fully
// buffered, so "libcprobe: checks passed" (printf) and then the handler's
// "libcprobe: atexit ran" (puts) only appear if exit() flushes stdout after
// running the handlers. A failed check prints itself (unbuffered) and the
// program exits non-zero.
//
// `libcprobe files` instead checks the file streams on a writable current
// directory: write, read back, append, update in place, positioning, rename
// and remove, through the real FAT32 layer, then sync.
#include "../../user/libk/koraos.h"

#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>

// user/libc/tests/setjmp_registers.S: 0 if a longjmp restores every
// callee-saved register, the frame pointer and the stack pointer.
long setjmp_registers(jmp_buf env);

static int failed;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "libcprobe: FAILED %s\n", what);
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

static jmp_buf jump;
static volatile sig_atomic_t caught;

__attribute__((noinline)) static void dive(int depth) {
    volatile char pad[64];  // a real frame each time, small: the stack is 4 KB
    pad[0] = (char)depth;
    if (depth == 0) {
        longjmp(jump, 7);
    }
    dive(depth - 1);
    pad[1] = pad[0];  // keeps the call from becoming a jump
}

static void on_signal(int sig) {
    caught = sig;
}

static void jump_out(int sig) {
    longjmp(jump, sig);
}

static void test_setjmp_and_signals(void) {
    volatile int passes = 0;
    volatile double kept = 2.5;
    int value = setjmp(jump);
    passes = passes + 1;
    if (value == 0) {
        dive(20);
    }
    check(value == 7 && passes == 2 && kept == 2.5, "longjmp out of nested calls");
    value = setjmp(jump);
    if (value == 0) {
        longjmp(jump, 0);
    }
    check(value == 1, "longjmp(env, 0) gives 1");
    static jmp_buf registers;
    check(setjmp_registers(registers) == 0, "longjmp restores the callee-saved registers");

    check(signal(SIGINT, on_signal) == SIG_DFL && raise(SIGINT) == 0 && caught == SIGINT,
          "raise calls the handler");
    check(signal(SIGINT, SIG_DFL) == on_signal, "signal returns the previous handler");
    errno = 0;
    check(signal(3, on_signal) == SIG_ERR && errno == EINVAL, "signal rejects other numbers");
    errno = 0;
    signal(SIGABRT, jump_out);
    value = setjmp(jump);
    if (value == 0) {
        abort();
    }
    check(value == SIGABRT, "abort raises SIGABRT and a handler can jump out");
    signal(SIGABRT, SIG_DFL);
}

// Volatile, so nothing below is computed by the compiler.
static volatile double half = 0.5, one = 1.0, two = 2.0, ten = 10.0, zero = 0.0;
static volatile float halff = 0.5f, twof = 2.0f, tenf = 10.0f;

static void bits64(double got, uint64_t want, const char *what) {
    uint64_t bits;
    memcpy(&bits, &got, sizeof bits);
    if (bits != want) {
        fprintf(stderr, "libcprobe: FAILED %s: %016lx, want %016lx\n", what, bits, want);
        failed = 1;
    }
}

static void bits32(float got, uint32_t want, const char *what) {
    uint32_t bits;
    memcpy(&bits, &got, sizeof bits);
    if (bits != want) {
        fprintf(stderr, "libcprobe: FAILED %s: %08x, want %08x\n", what, bits, want);
        failed = 1;
    }
}

// The functions src/math/aarch64 does in one instruction each, which CI's
// x86-64 hosts cannot run.
static void test_math_instructions(void) {
    double eps = DBL_EPSILON;
    check(sqrt(two) == 0x1.6a09e667f3bcdp+0 && sqrtf(twof) == 0x1.6a09e6p+0f && sqrt(-one) != sqrt(-one),
          "sqrt");
    check(fabs(-two) == 2.0 && !signbit(fabs(-zero)) && fabsf(-twof) == 2.0f, "fabs");
    check(floor(-half) == -1.0 && floor(two + half) == 2.0 && floorf(-halff) == -1.0f, "floor");
    check(ceil(half) == 1.0 && signbit(ceil(-half)) && ceil(-half) == 0.0 && ceilf(halff) == 1.0f,
          "ceil");
    check(trunc(-two - half) == -2.0 && truncf(twof + halff) == 2.0f, "trunc");
    check(round(two + half) == 3.0 && round(-half) == -1.0 && roundf(halff) == 1.0f, "round");
    check(rint(two + half) == 2.0 && rint(one + half) == 2.0 && rintf(twof + halff) == 2.0f, "rint");
    check(nearbyint(two + one + half) == 4.0 && nearbyintf(halff) == 0.0f, "nearbyint");
    check(lrint(two + half) == 2 && lrintf(twof + halff + 1) == 4 && llrint(-two - one - half) == -4 &&
              llrintf(halff) == 0,
          "lrint, llrint");
    check(lround(two + half) == 3 && lroundf(-halff) == -1 && llround(-two - half) == -3 &&
              llroundf(twof + halff) == 3 && llround(ten * 1e17) == 1000000000000000000ll,
          "lround, llround");
    check(fma(one + eps, one - eps, -one) == -eps * eps && fmaf(twof, twof, halff) == 4.5f,
          "fma rounds once");
    check(fmax(one, two) == 2.0 && fmax(zero / zero, one) == 1.0 && fmaxf(halff, twof) == 2.0f,
          "fmax");
    check(fmin(one, two) == 1.0 && fmin(one, zero / zero) == 1.0 && fminf(halff, twof) == 0.5f,
          "fmin");
}

// musl's functions: the bits the same code gives on the hosts, where the host
// unit tests compare it with the host's libm. A difference here means the
// target build computes something else (another instruction selection, a
// fused multiply-add, a wrong table).
static void test_math_values(void) {
    bits64(sin(half), 0x3fdeaee8744b05f0ull, "sin");
    bits64(cos(half), 0x3fec1528065b7d50ull, "cos");
    bits64(tan(half), 0x3fe17b4f5bf3474aull, "tan");
    bits64(asin(half), 0x3fe0c152382d7366ull, "asin");
    bits64(acos(half), 0x3ff0c152382d7366ull, "acos");
    bits64(atan(half), 0x3fddac670561bb4full, "atan");
    bits64(atan2(one, two), 0x3fddac670561bb4full, "atan2");
    bits64(sinh(one), 0x3ff2cd9fc44eb982ull, "sinh");
    bits64(cosh(one), 0x3ff8b07551d9f550ull, "cosh");
    bits64(tanh(half), 0x3fdd9353d7568af3ull, "tanh");
    bits64(asinh(two), 0x3ff719218313d087ull, "asinh");
    bits64(acosh(two), 0x3ff5124271980434ull, "acosh");
    bits64(atanh(half), 0x3fe193ea7aad030aull, "atanh");
    bits64(exp(one), 0x4005bf0a8b145769ull, "exp");
    bits64(exp2(half), 0x3ff6a09e667f3bcdull, "exp2");
    bits64(expm1(half), 0x3fe4c2531c3c0d38ull, "expm1");
    bits64(log(ten), 0x40026bb1bbb55516ull, "log");
    bits64(log2(ten), 0x400a934f0979a371ull, "log2");
    bits64(log10(two), 0x3fd34413509f79ffull, "log10");
    bits64(log1p(half), 0x3fd9f323ecbf984cull, "log1p");
    bits64(pow(two, half), 0x3ff6a09e667f3bcdull, "pow");
    bits64(pow(ten, -ten), 0x3ddb7cdfd9d7bdbbull, "pow with a negative exponent");
    bits64(cbrt(two), 0x3ff428a2f98d728bull, "cbrt");
    bits64(hypot(one, two), 0x4001e3779b97f4a8ull, "hypot");
    bits64(erf(half), 0x3fe0a7ef5c18edd2ull, "erf");
    bits64(erfc(two), 0x3f7328f5ec350e66ull, "erfc");
    bits64(tgamma(ten * half - half), 0x40274371e7866c65ull, "tgamma");
    bits64(lgamma(ten + half), 0x402be199a0f64393ull, "lgamma");
    // 1e22 needs the reduction by pi to hundreds of bits, on the 4 KB stack.
    bits64(sin(ten * 1e21), 0xbfeb453ab76bf397ull, "sin of 1e22");
    bits64(cos(ten * 1e21), 0x3fe0be2cef01c8f4ull, "cos of 1e22");
    bits64(fmod(ten, two + half), 0, "fmod");
    bits64(remainder(ten, two + one), 0x3ff0000000000000ull, "remainder");
    bits32(sinf(halff), 0x3ef57744u, "sinf");
    bits32(cosf(halff), 0x3f60a940u, "cosf");
    bits32(tanf(halff), 0x3f0bda7bu, "tanf");
    bits32(atan2f(halff, twof), 0x3e7adbb0u, "atan2f");
    bits32(expf(halff), 0x3fd3094cu, "expf");
    bits32(logf(tenf), 0x40135d8eu, "logf");
    bits32(log2f(tenf), 0x40549a78u, "log2f");
    bits32(powf(twof, halff), 0x3fb504f3u, "powf");
    bits32(sinhf(halff), 0x3f056680u, "sinhf");
    bits32(cbrtf(twof), 0x3fa14518u, "cbrtf");
    bits32(erff(halff), 0x3f053f7bu, "erff");
    bits32(tgammaf(tenf * halff - halff), 0x413a1b8fu, "tgammaf");
    bits32(sinf(tenf * 1e21f), 0xbf3becc4u, "sinf of 1e22");
    bits32(hypotf(halff, twof), 0x4003f07bu, "hypotf");

    int e = 0;
    double whole = 0;
    check(frexp(ten, &e) == 0.625 && e == 4 && ldexp(half, 11) == 1024.0 && ilogb(ten) == 3 &&
              modf(-two - half, &whole) == -0.5 && whole == -2.0 && scalbn(one, -1074) == DBL_TRUE_MIN,
          "frexp, ldexp, ilogb, modf, scalbn");
    check(nextafter(one, two) == 1.0 + DBL_EPSILON && copysign(two, -zero) == -2.0 &&
              fdim(one, two) == 0.0 && pow(zero / zero, zero) == 1.0 && isnan(log(-one)) &&
              isinf(exp(ten * ten * ten)) && fpclassify(DBL_TRUE_MIN * one) == FP_SUBNORMAL,
          "special values and the classification macros");
}

// long double is binary128 here, with no compiler-rt: even comparing two of
// them is a library call, so values are built and checked as bits. The
// functions compute in double; fabsl and copysignl are exact.
static long double quad(uint64_t high, uint64_t low) {
    uint64_t words[2] = {low, high};
    long double value;
    memcpy(&value, words, sizeof value);
    return value;
}

static int quad_is(long double value, uint64_t high, uint64_t low) {
    uint64_t words[2];
    memcpy(words, &value, sizeof words);
    return words[1] == high && words[0] == low;
}

static void test_math_long_double(void) {
    check(quad_is(sqrtl(2.0L), 0x3fff6a09e667f3bcull, 0xd000000000000000ull), "sqrtl(2)");
    check(quad_is(powl(2.0L, 10.0L), 0x4009000000000000ull, 0), "powl(2, 10)");
    check(quad_is(sinl(0.5L), 0x3ffdeaee8744b05full, 0), "sinl(0.5) is sin(0.5) widened");
    long double whole = 0.0L;
    check(quad_is(modfl(-3.75L, &whole), 0xbffe800000000000ull, 0) &&
              quad_is(whole, 0xc000800000000000ull, 0),
          "modfl");
    check(lrintl(2.5L) == 2 && llroundl(-2.5L) == -3 && ilogbl(1024.5L) == 10, "lrintl, ilogbl");
    // -(1 + 2^-112) is no double: narrowing it would lose the last bit.
    check(quad_is(fabsl(quad(0xbfff000000000000ull, 1)), 0x3fff000000000000ull, 1) &&
              quad_is(copysignl(quad(0x3fff000000000000ull, 1), -0.0L), 0xbfff000000000000ull, 1),
          "fabsl and copysignl are exact");
    check(isnan(nanl("")) && fpclassify(1.5L) == FP_NORMAL && isinf(HUGE_VALL) && signbit(-1.5L) &&
              !isfinite(HUGE_VALL) && fpclassify(0.0L) == FP_ZERO,
          "classification of long doubles");
    // nexttoward compares x with y exactly: 1 + 2^-100 and 1 - 2^-100 both
    // round to the double 1, but lie on either side of it.
    long double above = quad(0x3fff000000000000ull, 1ull << 12);
    long double below = quad(0x3ffeffffffffffffull, 0xffffffffffffe000ull);
    check(nexttoward(one, above) == 1.0 + DBL_EPSILON && nexttoward(one, below) == 1.0 - DBL_EPSILON / 2 &&
              nexttoward(one, 1.0L) == 1.0 && nexttoward(zero, -1.0L) == -DBL_TRUE_MIN &&
              isnan(nexttoward(one, nanl(""))),
          "nexttoward compares with the long double exactly");
    check(nexttowardf(twof - 1, above) == 1.0f + FLT_EPSILON &&
              nexttowardf(twof - 1, below) == 1.0f - FLT_EPSILON / 2,
          "nexttowardf");
}

static void test_files(void) {
    const char *name = "libc probe.txt", *moved = "libc probe moved.txt";
    FILE *f = fopen(name, "w");
    check(f != NULL, "fopen w");
    if (f == NULL) {
        return;
    }
    long total = 0, line10 = 0;  // the file size, and where line 10 starts
    for (int i = 0; i < 200; i++) {  // more than one buffer and one cluster's worth
        if (i == 10) {
            line10 = total;
        }
        total += fprintf(f, "line %03d %.3f\n", i, i / 8.0);
    }
    check(fclose(f) == 0, "fclose after writing");

    f = fopen(name, "r");
    char line[64];
    int lines = 0, good = 1;
    while (fgets(line, sizeof line, f) != NULL) {
        char *end;
        good &= strtol(line + 5, &end, 10) == lines && strtod(end, NULL) == lines / 8.0;
        lines++;
    }
    check(lines == 200 && good && feof(f) && !ferror(f), "read back with fgets");
    check(fseek(f, 0, SEEK_END) == 0 && ftell(f) == total, "ftell at the end");
    check(fseek(f, line10, SEEK_SET) == 0 && fgets(line, sizeof line, f) != NULL &&
              strcmp(line, "line 010 1.250\n") == 0,
          "fseek to a line");
    fclose(f);

    f = fopen(name, "a");
    fputs("appended\n", f);
    fclose(f);
    f = fopen(name, "r+");
    check(fseek(f, -9, SEEK_END) == 0 && fgetc(f) == 'a', "the appended line is last");
    fseek(f, 0, SEEK_SET);
    fputs("LINE", f);  // overwrite in place
    check(fseek(f, 0, SEEK_SET) == 0 && fgets(line, sizeof line, f) != NULL &&
              strcmp(line, "LINE 000 0.000\n") == 0,
          "r+ overwrote in place");
    fclose(f);

    remove(moved);
    check(rename(name, moved) == 0 && fopen(name, "r") == NULL && errno == ENOENT,
          "rename, and the old name is gone");
    check(remove(moved) == 0 && fopen(moved, "r") == NULL, "remove");
    check(mkdir("libc probe dir") == 0 && remove("libc probe dir") == 0, "remove a directory");
    // Make it durable, as every mutating tool must, and report a failure.
    check(sync() == 0, "sync");
}

static void at_exit_handler(void) {
    puts("libcprobe: atexit ran");
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "files") == 0) {
        test_files();
        if (!failed) {
            printf("libcprobe: files checked\n");
        }
        return failed;
    }
    check(setvbuf(stdout, NULL, _IOFBF, 0) == 0, "setvbuf");
    test_integers();
    test_floats();
    test_sort();
    test_heap();
    test_snprintf();
    test_strtod();
    test_setjmp_and_signals();
    test_math_instructions();
    test_math_values();
    test_math_long_double();
    if (failed) {
        return 1;
    }
    check(atexit(at_exit_handler) == 0, "atexit");
    printf("libcprobe: %s\n", "checks passed");
    return 0;  // crt0 passes this to exit(): the handler runs, then stdout is flushed
}
