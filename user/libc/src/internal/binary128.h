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
// an AArch64 long double. __binary128_to_double() narrows the other way,
// rounding to nearest, ties to even (what a hardware conversion would do).
// Host tests check the bit patterns directly.
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

static inline double __binary128_to_double(const unsigned long long in[2]) {
    unsigned long long hi = in[1], lo = in[0];
    unsigned long long sign = hi >> 63;
    long exp = (long)((hi >> 48) & 0x7fff);
    // The 112 fraction bits as hi48:lo64.
    unsigned long long frac_hi = hi & ((1ull << 48) - 1);
    unsigned long long bits;

    if (exp == 0x7fff) {
        // Infinity, or a NaN that stays a (quiet) NaN with its top payload bits.
        unsigned long long payload = (frac_hi << 4) | (lo >> 60);
        if (frac_hi != 0 || lo != 0) {
            payload |= 1ull << 51;
        }
        bits = (sign << 63) | (0x7ffull << DOUBLE_FRAC_BITS) | payload;
    } else {
        // Significand with its leading one (none for zero and subnormals,
        // which are far below any double and round to zero) as 113 bits:
        // keep the top 53 in m, the rest decides the rounding.
        long e = exp - BINARY128_EXP_BIAS + DOUBLE_EXP_BIAS;  // biased double exponent
        unsigned long long m = (exp != 0 ? 1ull << 52 : 0) | (frac_hi << 4) | (lo >> 60);
        unsigned long long rest = lo << 4;  // the 60 bits below m, at the top
        int sticky = 0;  // any bit set below `rest`
        if (exp == 0 || e <= 0) {
            // A double subnormal (or zero): value = m >> shift units of 2^-1074.
            long shift = exp == 0 ? 64 : 1 - e;
            sticky = rest != 0;
            if (shift >= 64) {
                // Below half the smallest subnormal (m < 2^53): rounds to zero.
                sticky |= m != 0;
                rest = 0;
                m = 0;
            } else {
                rest = m << (64 - shift);
                m >>= shift;
            }
            e = 0;
        }
        // Round to nearest, ties to even: `rest` holds the dropped bits with
        // the half-unit bit at the top.
        const unsigned long long half = 1ull << 63;
        if (rest > half || (rest == half && (sticky || (m & 1)))) {
            m++;
        }
        // m may have carried into the next binade (or a subnormal into the
        // normals): the exponent field absorbs it when m is added below.
        if (e == 0) {
            bits = (sign << 63) | m;  // a carry to 1 << 52 is the smallest normal
        } else {
            if (m >> 53) {
                m >>= 1;
                e++;
            }
            if (e >= 0x7ff) {
                bits = (sign << 63) | (0x7ffull << DOUBLE_FRAC_BITS);  // overflow: infinity
            } else {
                bits = (sign << 63) | ((unsigned long long)e << DOUBLE_FRAC_BITS) |
                       (m & ((1ull << DOUBLE_FRAC_BITS) - 1));
            }
        }
    }
    double d;
    __builtin_memcpy(&d, &bits, sizeof d);
    return d;
}

#endif // LIBC_BINARY128_H
