// SPDX-License-Identifier: GPL-3.0-or-later
//
// strtod, strtof, strtold and atof (C11 7.22.1): decimal and hexadecimal
// floating-point numbers, infinities and NaNs, correctly rounded (to nearest,
// ties to even) straight into the target format, so strtof never rounds
// twice. Only integer arithmetic is used.
//
// A decimal number is read as an integer N of up to MAX_DIGITS significant
// digits, in base 10^9 words, times 10^e10; later digits only matter as a
// "sticky" flag (nonzero or not), and 800 digits are more than the 767 that
// can separate a double from the midpoint of its neighbours. The value is
// then made exact in the form Q * 2^e2 + (sticky fraction): for e10 > 0 by
// multiplying N by 10^e10 (at most 310 digits); for e10 < 0 by multiplying
// by 2^29 until dividing by 10^-e10 still leaves three words (2^59), then
// dropping words. Every multiplication comes before any truncation, so a
// dropped remainder can only ever mean "a little more". Finally the top 64
// bits of Q and the sticky flag round to the format.
//
// strtold parses as a double and widens it (src/internal/binary128.h): long
// double values have double precision.

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "binary128.h"

#define WORD_BASE 1000000000u
#define WORD_DIGITS 9
#define MAX_DIGITS 800
// N * 2^s just before the division by 10^-e10: up to 800 + 340 digits.
#define MAX_WORDS ((MAX_DIGITS + 340) / WORD_DIGITS + 8)

struct format {
    int frac_bits;  // stored fraction bits: 52 or 23
    int exp_bits;   // 11 or 8
};

static const struct format double_format = {52, 11};
static const struct format float_format = {23, 8};

// Rounds m * 2^e2 (+ a little more if sticky), m != 0, to the format, and
// returns its bits. Sets *range on overflow, and on a subnormal or zero
// result that is not exact.
static uint64_t round_to_format(const struct format *f, uint64_t m, long e2, int sticky,
                                int *range) {
    int lz = __builtin_clzll(m);
    m <<= lz;  // exact: the top bit is now set
    e2 -= lz;
    long exp = e2 + 63;  // the value is in [2^exp, 2^(exp + 1))
    long bias = (1l << (f->exp_bits - 1)) - 1;
    long emin = 1 - bias;
    int precision = f->frac_bits + 1;
    // Bits of m to drop: the excess over the precision, plus more for a
    // subnormal result.
    long drop = 64 - precision + (exp < emin ? emin - exp : 0);
    uint64_t q, rest_nonzero;
    int up;
    if (drop >= 65) {
        q = 0;
        up = 0;  // below half the smallest subnormal
        rest_nonzero = 1;
    } else if (drop == 64) {
        q = 0;  // m's top bit is exactly the half
        rest_nonzero = 1;
        up = (m << 1) != 0 || sticky;
    } else {
        q = m >> drop;
        uint64_t rest = m & ((1ull << drop) - 1);
        uint64_t half = 1ull << (drop - 1);
        rest_nonzero = rest != 0 || sticky;
        up = rest > half || (rest == half && (sticky || (q & 1)));
    }
    q += (uint64_t)up;
    uint64_t frac_mask = (1ull << f->frac_bits) - 1;
    if (exp < emin) {
        // A subnormal; a carry up to 1 << frac_bits is the smallest normal,
        // which the encoding below gives directly.
        if (rest_nonzero) {
            *range = 1;
        }
        return q;
    }
    if (q >> precision) {
        q >>= 1;  // rounded up into the next binade
        exp++;
    }
    if (exp > bias) {
        *range = 1;
        return (uint64_t)((1l << f->exp_bits) - 1) << f->frac_bits;  // infinity
    }
    return ((uint64_t)(exp + bias) << f->frac_bits) | (q & frac_mask);
}

struct big {
    uint32_t w[MAX_WORDS];  // base 10^9, least significant first
    int n;
};

static void big_mul(struct big *b, uint32_t factor) {
    uint64_t carry = 0;
    for (int i = 0; i < b->n; i++) {
        uint64_t v = (uint64_t)b->w[i] * factor + carry;
        b->w[i] = (uint32_t)(v % WORD_BASE);
        carry = v / WORD_BASE;
    }
    if (carry != 0) {
        b->w[b->n++] = (uint32_t)carry;
    }
}

// Divides by 2^29; returns whether the remainder was nonzero.
static int big_div_pow2(struct big *b) {
    uint64_t rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        uint64_t v = rem * WORD_BASE + b->w[i];
        b->w[i] = (uint32_t)(v >> 29);
        rem = v & ((1u << 29) - 1);
    }
    while (b->n > 0 && b->w[b->n - 1] == 0) {
        b->n--;
    }
    return rem != 0;
}

static const uint32_t pow10[WORD_DIGITS] = {1,      10,      100,      1000,     10000,
                                            100000, 1000000, 10000000, 100000000};

// N * 10^e10 (+ sticky) to the format's bits.
static uint64_t decimal_to_bits(const struct format *f, struct big *b, long e10, int sticky,
                                int *range) {
    long e2 = 0;
    // Make e10 a multiple of 9, so whole words can be added or dropped.
    long r = ((e10 % WORD_DIGITS) + WORD_DIGITS) % WORD_DIGITS;
    if (r != 0) {
        big_mul(b, pow10[r]);
        e10 -= r;
    }
    if (e10 > 0) {
        // Times 10^e10, a word (10^9) at a time: exact, at most 310 digits.
        int words = (int)(e10 / WORD_DIGITS);
        memmove(b->w + words, b->w, (size_t)b->n * sizeof b->w[0]);
        memset(b->w, 0, (size_t)words * sizeof b->w[0]);
        b->n += words;
    } else if (e10 < 0) {
        // Scale up first, then divide by 10^-e10 by dropping words.
        int words = (int)(-e10 / WORD_DIGITS);
        while (b->n - words < 3) {
            big_mul(b, 1u << 29);
            e2 -= 29;
        }
        for (int i = 0; i < words; i++) {
            sticky |= b->w[i] != 0;
        }
        memmove(b->w, b->w + words, (size_t)(b->n - words) * sizeof b->w[0]);
        b->n -= words;
    }
    while (b->n > 3) {
        sticky |= big_div_pow2(b);
        e2 += 29;
    }
    // At most 3 words: below 10^27, so 90 bits; keep the top 64.
    unsigned __int128 v = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        v = v * WORD_BASE + b->w[i];
    }
    while (v >> 64) {
        sticky |= (int)(v & 1);
        v >>= 1;
        e2++;
    }
    return round_to_format(f, (uint64_t)v, e2, sticky, range);
}

static int hex_value(unsigned char c) {
    if (isdigit(c)) {
        return c - '0';
    }
    c = (unsigned char)tolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

// Case-insensitive prefix match.
static int starts_with(const char *s, const char *word) {
    for (; *word; s++, word++) {
        if (tolower((unsigned char)*s) != *word) {
            return 0;
        }
    }
    return 1;
}

// An exponent: optional sign and digits, clamped far beyond any format's
// range. Returns the position after it, or `s` itself if there is none.
static const char *read_exponent(const char *s, long *out) {
    const char *p = s;
    int negative = 0;
    if (*p == '+' || *p == '-') {
        negative = *p++ == '-';
    }
    if (!isdigit((unsigned char)*p)) {
        return s;
    }
    long x = 0;
    for (; isdigit((unsigned char)*p); p++) {
        if (x < 100000) {
            x = x * 10 + (*p - '0');
        }
    }
    *out = negative ? -x : x;
    return p;
}

// The common parser: returns the bits of the result in format f and leaves
// *endptr after the number (at nptr when there is none).
static uint64_t parse(const struct format *f, const char *nptr, char **endptr) {
    const char *s = nptr;
    uint64_t sign = 0;
    int range = 0;
    uint64_t bits = 0;
    const uint64_t sign_bit = 1ull << (f->frac_bits + f->exp_bits);
    const uint64_t exp_all_ones = ((1ull << f->exp_bits) - 1) << f->frac_bits;

    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '+' || *s == '-') {
        sign = *s++ == '-' ? sign_bit : 0;
    }

    if (starts_with(s, "inf")) {
        s += starts_with(s, "infinity") ? 8 : 3;
        bits = exp_all_ones;
        goto done;
    }
    if (starts_with(s, "nan")) {
        s += 3;
        if (*s == '(') {  // nan(n-char-sequence): consumed only when well formed
            const char *p = s + 1;
            while (isalnum((unsigned char)*p) || *p == '_') {
                p++;
            }
            if (*p == ')') {
                s = p + 1;
            }
        }
        bits = exp_all_ones | (1ull << (f->frac_bits - 1));  // a quiet NaN
        goto done;
    }

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        (hex_value((unsigned char)s[2]) >= 0 ||
         (s[2] == '.' && hex_value((unsigned char)s[3]) >= 0))) {
        // Hexadecimal: up to 16 significant digits in m, the rest sticky.
        s += 2;
        uint64_t m = 0;
        long e2 = 0;
        int digits = 0, sticky = 0, seen_point = 0;
        for (;; s++) {
            if (*s == '.' && !seen_point) {
                seen_point = 1;
                continue;
            }
            int v = hex_value((unsigned char)*s);
            if (v < 0) {
                break;
            }
            if (digits < 16) {
                if (m != 0 || v != 0) {
                    m = m << 4 | (uint64_t)v;
                    digits++;
                }
                if (seen_point) {
                    e2 -= 4;
                }
            } else {
                sticky |= v != 0;
                if (!seen_point) {
                    e2 += 4;
                }
            }
        }
        if (*s == 'p' || *s == 'P') {
            long x = 0;
            const char *after = read_exponent(s + 1, &x);
            if (after != s + 1) {
                s = after;
                e2 += x;
            }
        }
        if (m != 0) {
            bits = round_to_format(f, m, e2, sticky, &range);
        }
        goto done;
    }

    {
        // Decimal: the significant digits, most significant first, as words.
        struct big b;
        b.n = 0;
        uint32_t word = 0;
        int in_word = 0, ndigits = 0, sticky = 0, any = 0, seen_point = 0;
        long e10 = 0;
        for (;; s++) {
            if (*s == '.' && !seen_point) {
                seen_point = 1;
                continue;
            }
            if (!isdigit((unsigned char)*s)) {
                break;
            }
            any = 1;
            int v = *s - '0';
            if (ndigits == 0 && v == 0) {
                if (seen_point) {
                    e10--;  // a leading zero after the point
                }
                continue;
            }
            if (ndigits < MAX_DIGITS) {
                word = word * 10 + (uint32_t)v;
                ndigits++;
                if (++in_word == WORD_DIGITS) {
                    b.w[b.n++] = word;
                    word = 0;
                    in_word = 0;
                }
                if (seen_point) {
                    e10--;
                }
            } else {
                sticky |= v != 0;
                if (!seen_point) {
                    e10++;
                }
            }
        }
        if (!any) {
            s = nptr;  // no digits: no number, and a positive zero
            sign = 0;
            goto done;
        }
        if (*s == 'e' || *s == 'E') {
            long x = 0;
            const char *after = read_exponent(s + 1, &x);
            if (after != s + 1) {
                s = after;
                e10 += x;
            }
        }
        if (ndigits == 0) {
            goto done;  // zero
        }
        if (in_word != 0) {
            // Complete the last word with zeros; the exponent compensates.
            b.w[b.n++] = word * pow10[WORD_DIGITS - in_word];
            e10 -= WORD_DIGITS - in_word;
        }
        // N has exactly b.n * 9 digits (the first nonzero), so the value is
        // in [10^(magnitude - 1), 10^magnitude).
        long magnitude = (long)b.n * WORD_DIGITS + e10;
        long max10 = f->exp_bits == 11 ? 310 : 40;
        long min10 = f->exp_bits == 11 ? -330 : -50;
        if (magnitude > max10) {
            bits = exp_all_ones;  // far too large: infinity
            range = 1;
            goto done;
        }
        if (magnitude < min10) {
            range = 1;  // far too small: zero
            goto done;
        }
        // Words to least significant first.
        for (int i = 0, j = b.n - 1; i < j; i++, j--) {
            uint32_t t = b.w[i];
            b.w[i] = b.w[j];
            b.w[j] = t;
        }
        bits = decimal_to_bits(f, &b, e10, sticky, &range);
    }

done:
    if (endptr) {
        *endptr = (char *)s;
    }
    if (range) {
        errno = ERANGE;
    }
    return sign | bits;
}

double strtod(const char *restrict nptr, char **restrict endptr) {
    uint64_t bits = parse(&double_format, nptr, endptr);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

float strtof(const char *restrict nptr, char **restrict endptr) {
    uint32_t bits = (uint32_t)parse(&float_format, nptr, endptr);
    float v;
    memcpy(&v, &bits, sizeof v);
    return v;
}

long double strtold(const char *restrict nptr, char **restrict endptr) {
    double d = strtod(nptr, endptr);
#if defined(__aarch64__) && __LDBL_MANT_DIG__ == 113
    union {
        unsigned long long words[2];
        long double value;
    } wide;
    __double_to_binary128(d, wide.words);
    return wide.value;
#else
    return d;  // a host long double the hardware converts to (x87, or double itself)
#endif
}

double atof(const char *nptr) {
    return strtod(nptr, NULL);
}
