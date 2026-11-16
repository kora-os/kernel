// SPDX-License-Identifier: GPL-3.0-or-later
//
// For the math host test on machines that are not arm64. The libc does
// fifteen functions (and their float forms) in a single AArch64 instruction
// each (src/math/aarch64), which such a host cannot run. The rest of the
// library calls some of them (sqrt, floor, scalbn's helpers, ...), so here
// they are, taken from the host's own libm by symbol name. host_rename.h
// renames each definition to the libc's name (sqrt is kora_sqrt).

// The name is stringified here, in the macro that receives it: passed on to
// another macro it would be renamed first.
#define HOST_STRINGIFY2(x) #x
#define HOST_STRINGIFY(x) HOST_STRINGIFY2(x)
#define HOST_PREFIX HOST_STRINGIFY(__USER_LABEL_PREFIX__)

#define FROM_HOST_1(type, result, name)                  \
    result host_##name(type) __asm__(HOST_PREFIX #name); \
    result name(type x) {                                \
        return host_##name(x);                           \
    }

#define FROM_HOST_2(type, name)                          \
    type host_##name(type, type) __asm__(HOST_PREFIX #name); \
    type name(type x, type y) {                          \
        return host_##name(x, y);                        \
    }

FROM_HOST_1(double, double, ceil)
FROM_HOST_1(float, float, ceilf)
FROM_HOST_1(double, double, floor)
FROM_HOST_1(float, float, floorf)
FROM_HOST_1(double, double, nearbyint)
FROM_HOST_1(float, float, nearbyintf)
FROM_HOST_1(double, double, rint)
FROM_HOST_1(float, float, rintf)
FROM_HOST_1(double, double, round)
FROM_HOST_1(float, float, roundf)
FROM_HOST_1(double, double, trunc)
FROM_HOST_1(float, float, truncf)
FROM_HOST_1(double, double, fabs)
FROM_HOST_1(float, float, fabsf)
FROM_HOST_1(double, double, sqrt)
FROM_HOST_1(float, float, sqrtf)
FROM_HOST_1(double, long, lrint)
FROM_HOST_1(float, long, lrintf)
FROM_HOST_1(double, long long, llrint)
FROM_HOST_1(float, long long, llrintf)
FROM_HOST_1(double, long, lround)
FROM_HOST_1(float, long, lroundf)
FROM_HOST_1(double, long long, llround)
FROM_HOST_1(float, long long, llroundf)
FROM_HOST_2(double, fmax)
FROM_HOST_2(float, fmaxf)
FROM_HOST_2(double, fmin)
FROM_HOST_2(float, fminf)

double host_fma(double, double, double) __asm__(HOST_PREFIX "fma");
double fma(double x, double y, double z) {
    return host_fma(x, y, z);
}

float host_fmaf(float, float, float) __asm__(HOST_PREFIX "fmaf");
float fmaf(float x, float y, float z) {
    return host_fmaf(x, y, z);
}
