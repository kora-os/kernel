// SPDX-License-Identifier: GPL-3.0-or-later
//
// The long double functions of <math.h>, at double precision. long double is
// IEEE binary128 on AArch64; the hardware cannot compute with it and there
// is no compiler-rt, so each function narrows its arguments to double (round
// to nearest), calls the double function and widens the result, moving bits
// with integer operations (src/internal/binary128.h). The same rule as
// strtold and the %L printf conversions. fabsl and copysignl only touch the
// sign bit, so they are exact.
//
// All of them are in this one file: a program calling any of them links the
// double functions they all use.
//
// On a host (the unit tests) long double is whatever the machine has, and
// plain conversions do the same job.

#include <float.h>
#include <math.h>

#include "binary128.h"

#if defined(__aarch64__) && LDBL_MANT_DIG == 113
#define LONG_DOUBLE_IS_BINARY128 1

union words {
    unsigned long long w[2];  // w[1] holds the sign and the exponent
    long double value;
};

static double narrow(long double x) {
    union words u;
    u.value = x;
    return __binary128_to_double(u.w);
}

static long double widen(double d) {
    union words u;
    __double_to_binary128(d, u.w);
    return u.value;
}
#else
static double narrow(long double x) {
    return (double)x;
}

static long double widen(double d) {
    return d;
}
#endif

#define UNARY(name)                         \
    long double name##l(long double x) {    \
        return widen(name(narrow(x)));      \
    }

#define BINARY(name)                                   \
    long double name##l(long double x, long double y) { \
        return widen(name(narrow(x), narrow(y)));      \
    }

#define TO_INTEGER(type, name)              \
    type name##l(long double x) {           \
        return name(narrow(x));             \
    }

UNARY(acos)
UNARY(asin)
UNARY(atan)
BINARY(atan2)
UNARY(cos)
UNARY(sin)
UNARY(tan)

UNARY(acosh)
UNARY(asinh)
UNARY(atanh)
UNARY(cosh)
UNARY(sinh)
UNARY(tanh)

UNARY(exp)
UNARY(exp2)
UNARY(expm1)
UNARY(log)
UNARY(log10)
UNARY(log1p)
UNARY(log2)
UNARY(logb)
TO_INTEGER(int, ilogb)

long double frexpl(long double value, int *exp) {
    return widen(frexp(narrow(value), exp));
}

long double ldexpl(long double x, int exp) {
    return widen(ldexp(narrow(x), exp));
}

long double scalbnl(long double x, int n) {
    return widen(scalbn(narrow(x), n));
}

long double scalblnl(long double x, long int n) {
    return widen(scalbln(narrow(x), n));
}

long double modfl(long double value, long double *iptr) {
    double whole;
    double fraction = modf(narrow(value), &whole);
    *iptr = widen(whole);
    return widen(fraction);
}

UNARY(cbrt)
BINARY(hypot)
BINARY(pow)
UNARY(sqrt)

UNARY(erf)
UNARY(erfc)
UNARY(lgamma)
UNARY(tgamma)

UNARY(ceil)
UNARY(floor)
UNARY(nearbyint)
UNARY(rint)
UNARY(round)
UNARY(trunc)
TO_INTEGER(long int, lrint)
TO_INTEGER(long long int, llrint)
TO_INTEGER(long int, lround)
TO_INTEGER(long long int, llround)

BINARY(fmod)
BINARY(remainder)

long double remquol(long double x, long double y, int *quo) {
    return widen(remquo(narrow(x), narrow(y), quo));
}

long double nanl(const char *tagp) {
    return widen(nan(tagp));
}

// Both arguments are long double already, so these two are the same function:
// the next double after x in the direction of y.
BINARY(nextafter)

long double nexttowardl(long double x, long double y) {
    return widen(nextafter(narrow(x), narrow(y)));
}

BINARY(fdim)
BINARY(fmax)
BINARY(fmin)

long double fmal(long double x, long double y, long double z) {
    return widen(fma(narrow(x), narrow(y), narrow(z)));
}

#ifdef LONG_DOUBLE_IS_BINARY128
long double fabsl(long double x) {
    union words u;
    u.value = x;
    u.w[1] &= ~(1ull << 63);
    return u.value;
}

long double copysignl(long double x, long double y) {
    union words ux, uy;
    ux.value = x;
    uy.value = y;
    ux.w[1] = (ux.w[1] & ~(1ull << 63)) | (uy.w[1] & (1ull << 63));
    return ux.value;
}
#else
UNARY(fabs)
BINARY(copysign)
#endif
