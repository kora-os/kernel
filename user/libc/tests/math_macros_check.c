// SPDX-License-Identifier: GPL-3.0-or-later
//
// Built without optimization and linked against the libc by libc_link_check
// (see user/libc/CMakeLists.txt); never run. The classification macros of
// <math.h> must not need compiler-rt for any argument type: KoraOS has none,
// and long double is binary128, where a comparison or a conversion is a
// library call (__unordtf2, __extenddftf2). Without optimization a compiler
// also emits code for branches it can see are never taken, which is how a
// first version of these macros failed to link in user programs built -O0.

#include <math.h>

volatile float f = 1.5f;
volatile double d = 1.5;
volatile long double l = 1.5L;

static int classify_float(void) {
    return fpclassify(f) + isfinite(f) + isinf(f) + isnan(f) + isnormal(f) + signbit(f);
}

static int classify_double(void) {
    return fpclassify(d) + isfinite(d) + isinf(d) + isnan(d) + isnormal(d) + signbit(d);
}

static int classify_long_double(void) {
    return fpclassify(l) + isfinite(l) + isinf(l) + isnan(l) + isnormal(l) + signbit(l);
}

static int compare(void) {
    return isgreater(d, f) + isgreaterequal(d, d) + isless(f, f) + islessequal(d, f) +
           islessgreater(d, d) + isunordered(f, d);
}

int main(void) {
    return classify_float() + classify_double() + classify_long_double() + compare();
}
