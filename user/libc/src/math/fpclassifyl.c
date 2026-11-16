// SPDX-License-Identifier: GPL-3.0-or-later
//
// The class of a long double, behind fpclassify(), isnan() and the other
// classification macros of <math.h> when their argument is a long double.
// On AArch64 that is a binary128, and the compiler's own classification
// compares it, which is a compiler-rt call (__unordtf2); the bits say the
// same without one. In a file of its own: a program that only classifies
// does not link the rest of the long double functions.

#include <float.h>
#include <math.h>

int __fpclassifyl(long double x);

int __fpclassifyl(long double x) {
#if LDBL_MANT_DIG == 113
    union {
        unsigned long long w[2];  // little endian: w[1] has the sign and exponent
        long double value;
    } u;
    u.value = x;
    unsigned exponent = (unsigned)(u.w[1] >> 48) & 0x7fff;
    int fraction = (u.w[1] << 16) != 0 || u.w[0] != 0;
    if (exponent == 0x7fff) {
        return fraction ? FP_NAN : FP_INFINITE;
    }
    if (exponent == 0) {
        return fraction ? FP_SUBNORMAL : FP_ZERO;
    }
    return FP_NORMAL;
#else
    // A host long double (the unit tests): the machine can compare it.
    return __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x);
#endif
}
