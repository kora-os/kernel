// SPDX-License-Identifier: GPL-3.0-or-later
//
// long double on AArch64 is IEEE 754 binary128, which the hardware cannot
// compute with: every operation is a compiler-rt call (__addtf3, __multf3,
// __extenddftf2, ...), and KoraOS has no compiler-rt. The libc therefore never
// does arithmetic in long double; it computes in double and moves the bits.
//
// __double_to_binary128() widens a double exactly, with integer operations
// only: out[0] is the low 64 bits, out[1] the high 64 bits (sign, 15-bit
// exponent, top 48 fraction bits), which is the little-endian memory order of
// an AArch64 long double. Host tests check the bit patterns directly.
#ifndef LIBC_BINARY128_H
#define LIBC_BINARY128_H

#define DOUBLE_EXP_BIAS 1023
#define BINARY128_EXP_BIAS 16383
#define DOUBLE_FRAC_BITS 52
#define BINARY128_FRAC_BITS 112

static inline void __double_to_binary128(double d, unsigned long long out[2]) {
    unsigned long long bits;
    __builtin_memcpy(&bits, &d, sizeof bits);
    unsigned long long sign = bits >> 63;
    unsigned long long exp = (bits >> DOUBLE_FRAC_BITS) & 0x7ff;
    unsigned long long frac = bits & ((1ull << DOUBLE_FRAC_BITS) - 1);
    unsigned long long exp128;

    if (exp == 0x7ff) {
        exp128 = 0x7fff;  // infinity or NaN; the fraction (quiet bit) carries over
    } else if (exp == 0 && frac == 0) {
        exp128 = 0;  // signed zero
    } else if (exp == 0) {
        // A double subnormal is a normal binary128: shift the leading one up
        // to the implicit bit position and lower the exponent to match.
        int shift = 0;
        while (!(frac & (1ull << DOUBLE_FRAC_BITS))) {
            frac <<= 1;
            shift++;
        }
        frac &= (1ull << DOUBLE_FRAC_BITS) - 1;
        exp128 = (unsigned long long)(BINARY128_EXP_BIAS - DOUBLE_EXP_BIAS + 1 - shift);
    } else {
        exp128 = exp - DOUBLE_EXP_BIAS + BINARY128_EXP_BIAS;
    }

    // The 52 fraction bits become the top of the 112: 48 in the high word,
    // the remaining 4 at the top of the low word.
    const int widen = BINARY128_FRAC_BITS - DOUBLE_FRAC_BITS;  // 60
    out[0] = frac << widen;
    out[1] = (sign << 63) | (exp128 << 48) | (frac >> (64 - widen));
}

#endif // LIBC_BINARY128_H
