// SPDX-License-Identifier: GPL-3.0-or-later
//
// nexttoward and nexttowardf (C11 7.12.11.4): the next double (or float)
// after x in the direction of the long double y. The direction comes from
// comparing x with y exactly. On AArch64 that is a comparison of binary128
// values, which would be a compiler-rt call, so it is done on the bits: x
// widens to binary128 exactly, and two finite binary128 values compare like
// sign-and-magnitude integers.

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "binary128.h"

#if defined(__aarch64__) && LDBL_MANT_DIG == 113
union words {
    unsigned long long w[2];
    long double value;
};

static int is_nan(const unsigned long long w[2]) {
    return ((w[1] >> 48) & 0x7fff) == 0x7fff && ((w[1] << 16) != 0 || w[0] != 0);
}

// The value as an integer that orders like the number: the magnitude bits,
// negated for a negative number (so both zeros are 0).
static __int128 ordered(const unsigned long long w[2]) {
    __int128 magnitude = ((__int128)(w[1] & ~(1ull << 63)) << 64) | w[0];
    return (w[1] >> 63) ? -magnitude : magnitude;
}

// -1, 0 or 1 for x below, equal to or above y; 2 if y is a NaN.
static int compare(double x, long double y) {
    union words uy;
    unsigned long long wx[2];
    uy.value = y;
    if (is_nan(uy.w)) {
        return 2;
    }
    __double_to_binary128(x, wx);
    __int128 a = ordered(wx), b = ordered(uy.w);
    return a < b ? -1 : a > b;
}

static double narrow(long double y) {
    union words u;
    u.value = y;
    return __binary128_to_double(u.w);
}
#else
static int compare(double x, long double y) {
    if (y != y) {
        return 2;
    }
    return x < y ? -1 : x > y;
}

static double narrow(long double y) {
    return (double)y;
}
#endif

// Reading a volatile forces the operation, for the exception flags it raises.
static void raise_flags(double value) {
    volatile double forced = value;
    (void)forced;
}

double nexttoward(double x, long double y) {
    if (x != x) {
        return x;
    }
    int order = compare(x, y);
    if (order == 2) {
        return narrow(y);  // y is a NaN
    }
    if (order == 0) {
        return narrow(y);  // equal: y, which matters for the sign of zero
    }
    uint64_t bits;
    __builtin_memcpy(&bits, &x, sizeof bits);
    if (x == 0.0) {
        bits = 1 | (order < 0 ? 0 : 1ull << 63);  // the smallest subnormal, towards y
    } else if ((order < 0) == (x > 0.0)) {
        bits++;  // away from zero
    } else {
        bits--;
    }
    double result;
    __builtin_memcpy(&result, &bits, sizeof result);
    unsigned exponent = (bits >> 52) & 0x7ff;
    if (exponent == 0x7ff) {
        raise_flags(x + x);  // overflow
    } else if (exponent == 0) {
        raise_flags(x * x + result * result);  // underflow
    }
    return result;
}

float nexttowardf(float x, long double y) {
    if (x != x) {
        return x;
    }
    int order = compare(x, y);  // a float widens to double exactly
    if (order == 2 || order == 0) {
        return (float)narrow(y);
    }
    uint32_t bits;
    __builtin_memcpy(&bits, &x, sizeof bits);
    if (x == 0.0f) {
        bits = 1 | (order < 0 ? 0 : 1u << 31);
    } else if ((order < 0) == (x > 0.0f)) {
        bits++;
    } else {
        bits--;
    }
    float result;
    __builtin_memcpy(&result, &bits, sizeof result);
    unsigned exponent = (bits >> 23) & 0xff;
    if (exponent == 0xff) {
        raise_flags(x + x);
    } else if (exponent == 0) {
        raise_flags(x * x + result * result);
    }
    return result;
}
