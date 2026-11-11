// SPDX-License-Identifier: GPL-3.0-or-later
//
// abs, labs, llabs and div, ldiv, lldiv (C11 7.22.6). The absolute value of
// the most negative number is not representable; these return it unchanged
// (two's complement), computed in the unsigned type so it is not signed
// overflow. Division
// truncates towards zero, as C's / and % do.

#include <stdlib.h>

int abs(int j) {
    return j < 0 ? (int)(0u - (unsigned)j) : j;
}

long int labs(long int j) {
    return j < 0 ? (long)(0ul - (unsigned long)j) : j;
}

long long int llabs(long long int j) {
    return j < 0 ? (long long)(0ull - (unsigned long long)j) : j;
}

div_t div(int numer, int denom) {
    div_t r = {numer / denom, numer % denom};
    return r;
}

ldiv_t ldiv(long int numer, long int denom) {
    ldiv_t r = {numer / denom, numer % denom};
    return r;
}

lldiv_t lldiv(long long int numer, long long int denom) {
    lldiv_t r = {numer / denom, numer % denom};
    return r;
}
