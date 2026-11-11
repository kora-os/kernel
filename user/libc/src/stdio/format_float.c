// SPDX-License-Identifier: GPL-3.0-or-later
//
// The floating-point conversions of the printf engine: f F e E g G a A
// (C11 7.21.6.1). Output is exact: the double's binary value is expanded to
// all of its decimal digits (at most 767 significant ones) and rounded at the
// requested precision to nearest, ties to even, which is what the hardware
// rounding mode asks for. Only integer arithmetic is used.
//
// A double is m * 2^e with m < 2^53. For e >= 0 the value is the integer
// m * 2^e (at most 309 digits); for e < 0 it is m * 5^-e / 10^-e, so the
// digits are those of the integer m * 5^-e with the decimal point moved.
// Either integer is held in base 10^9 words, least significant first: 90
// words cover m * 5^1074, so about 360 bytes of stack.

#include <stdint.h>
#include <string.h>

#include "format_impl.h"

#define WORD_BASE 1000000000u
#define WORD_DIGITS 9
#define MAX_WORDS 90

static const uint32_t pow10[WORD_DIGITS + 1] = {1,      10,      100,      1000,      10000,
                                                100000, 1000000, 10000000, 100000000, 1000000000};

// The exact decimal value: 0.d0 d1 d2 ... * 10^point, with d0 != 0 unless
// the value is zero (no digits; point 1 so that it prints as "0").
struct decimal {
    uint32_t w[MAX_WORDS];  // least significant word first
    int nwords;
    int ndigits;
    int point;
};

static void multiply_small(struct decimal *d, uint32_t factor) {
    uint64_t carry = 0;
    for (int i = 0; i < d->nwords; i++) {
        uint64_t v = (uint64_t)d->w[i] * factor + carry;
        d->w[i] = (uint32_t)(v % WORD_BASE);
        carry = v / WORD_BASE;
    }
    while (carry != 0) {
        d->w[d->nwords++] = (uint32_t)(carry % WORD_BASE);
        carry /= WORD_BASE;
    }
}

static void expand(struct decimal *d, uint64_t m, int e) {
    d->nwords = 0;
    while (m != 0) {
        d->w[d->nwords++] = (uint32_t)(m % WORD_BASE);
        m /= WORD_BASE;
    }
    if (d->nwords == 0) {
        d->ndigits = 0;
        d->point = 1;
        return;
    }
    int k = 0;  // the value is the integer times 10^-k
    if (e >= 0) {
        for (; e >= 29; e -= 29) {
            multiply_small(d, 1u << 29);
        }
        multiply_small(d, 1u << e);
    } else {
        k = -e;
        int left = k;
        for (; left >= 13; left -= 13) {
            multiply_small(d, 1220703125u);  // 5^13
        }
        uint32_t f = 1;
        while (left-- > 0) {
            f *= 5;
        }
        multiply_small(d, f);
    }
    int top = 1;
    while (top < WORD_DIGITS && d->w[d->nwords - 1] >= pow10[top]) {
        top++;
    }
    d->ndigits = (d->nwords - 1) * WORD_DIGITS + top;
    d->point = d->ndigits - k;
}

// Digit i of 0.d0 d1 ...; zero outside the digits (also for negative i).
static int digit(const struct decimal *d, int i) {
    if (i < 0 || i >= d->ndigits) {
        return 0;
    }
    int from_right = d->ndigits - 1 - i;
    return (int)(d->w[from_right / WORD_DIGITS] / pow10[from_right % WORD_DIGITS] % 10);
}

// Whether any digit after index i is nonzero.
static int nonzero_after(const struct decimal *d, int i) {
    if (i >= d->ndigits - 1) {
        return 0;
    }
    if (i < -1) {
        i = -1;
    }
    int below = d->ndigits - 1 - i;  // digit positions 0 .. below-1, from the right
    for (int w = 0; w < below / WORD_DIGITS; w++) {
        if (d->w[w] != 0) {
            return 1;
        }
    }
    return below % WORD_DIGITS != 0 && d->w[below / WORD_DIGITS] % pow10[below % WORD_DIGITS] != 0;
}

// The digits rounded to keep indices below `cut`: if rounding up, digit `pos`
// grows by one and every kept digit after it becomes 0. pos is -1 when the
// carry runs out of the digits: the result then gains a leading 1 there.
struct rounded {
    const struct decimal *d;
    int cut;
    int up;
    int pos;
};

static void round_at(struct rounded *r, const struct decimal *d, int cut) {
    r->d = d;
    r->cut = cut;
    r->up = 0;
    r->pos = cut;
    int next = digit(d, cut);
    if (next > 5 || (next == 5 && (nonzero_after(d, cut) || (digit(d, cut - 1) & 1)))) {
        r->up = 1;
        int p = cut - 1;
        while (p >= 0 && digit(d, p) == 9) {
            p--;
        }
        r->pos = p;  // never below -1: digit(-1) is 0
    }
}

static int rdigit(const struct rounded *r, int i) {
    if (i >= r->cut) {
        return 0;
    }
    if (!r->up || i < r->pos) {
        return digit(r->d, i);
    }
    return i == r->pos ? digit(r->d, i) + 1 : 0;
}

// Index of the leading digit after rounding.
static int lead_index(const struct rounded *r) {
    return r->up && r->pos < 0 ? -1 : 0;
}

// Emits rounded digits from..to-1, a chunk at a time.
static void emit_digits(struct out *o, const struct rounded *r, int from, int to) {
    char chunk[32];
    while (from < to && !o->error) {
        int n = 0;
        while (n < (int)sizeof chunk && from < to) {
            chunk[n++] = (char)('0' + rdigit(r, from++));
        }
        __format_emit(o, chunk, (size_t)n);
    }
}


// What a conversion prints after the sign: for %f and %e, ranges of rounded
// digit indices; for %a, the hex digits themselves.
struct body {
    int style;  // 'f', 'e' or 'a'
    int upper;
    int point;  // print a decimal point
    int exponent;
    const struct rounded *r;
    int int_from, int_to;    // integer digit indices
    int int_zero;            // no integer digits: print a single 0
    int frac_from, frac_to;  // fraction digit indices
    unsigned hex_lead;       // %a: the digit before the point
    uint64_t hex_frac;       // %a: fraction digits, the first in bits 63..60
    int hex_digits;          // %a: fraction digits printed
};

static size_t decimal_len(int x) {
    unsigned u = x < 0 ? 0u - (unsigned)x : (unsigned)x;
    size_t n = 1;
    while (u >= 10) {
        u /= 10;
        n++;
    }
    return n;
}

// e+05, P-1022: the letter, a sign and at least `min_digits` digits.
static void emit_exponent(struct out *o, char letter, int x, size_t min_digits) {
    char buf[16];
    unsigned u = x < 0 ? 0u - (unsigned)x : (unsigned)x;
    size_t nd = decimal_len(x) < min_digits ? min_digits : decimal_len(x);
    buf[0] = letter;
    buf[1] = x < 0 ? '-' : '+';
    for (size_t i = 0; i < nd; i++) {
        buf[1 + nd - i] = (char)('0' + u % 10);
        u /= 10;
    }
    __format_emit(o, buf, 2 + nd);
}

static size_t body_len(const struct body *b) {
    size_t len = b->point ? 1 : 0;
    if (b->style == 'a') {
        return len + 3 + (size_t)b->hex_digits + 2 + decimal_len(b->exponent);  // 0x1 ... p+N
    }
    len += (size_t)(b->int_to - b->int_from) + (size_t)b->int_zero +
           (size_t)(b->frac_to - b->frac_from);
    if (b->style == 'e') {
        size_t x = decimal_len(b->exponent);
        len += 2 + (x < 2 ? 2 : x);
    }
    return len;
}

// The body; for %a, `prefix` says whether to start with 0x (a zero-padded
// field writes the prefix itself, before the zeros).
static void emit_body(struct out *o, const struct body *b, int prefix) {
    if (b->style == 'a') {
        const char *hex = b->upper ? "0123456789ABCDEF" : "0123456789abcdef";
        char buf[4];
        size_t n = 0;
        if (prefix) {
            __format_emit(o, b->upper ? "0X" : "0x", 2);
        }
        buf[n++] = hex[b->hex_lead];
        if (b->point) {
            buf[n++] = '.';
        }
        __format_emit(o, buf, n);
        uint64_t f = b->hex_frac;
        for (int i = 0; i < b->hex_digits; i++) {
            char c = hex[f >> 60];  // past the 13 fraction digits, f is 0
            f <<= 4;
            __format_emit(o, &c, 1);
        }
        emit_exponent(o, b->upper ? 'P' : 'p', b->exponent, 1);
        return;
    }
    if (b->int_zero) {
        __format_emit(o, "0", 1);
    }
    emit_digits(o, b->r, b->int_from, b->int_to);
    if (b->point) {
        __format_emit(o, ".", 1);
    }
    emit_digits(o, b->r, b->frac_from, b->frac_to);
    if (b->style == 'e') {
        emit_exponent(o, b->upper ? 'E' : 'e', b->exponent, 2);
    }
}

// Writes `sign` (0 for none) and the body (or, for infinities and NaNs,
// `text`), padded to the field width: with '0', zeros after the sign (and
// after 0x for %a); spaces otherwise, and always for infinities and NaNs.
static void emit_field(struct out *o, const struct spec *sp, char sign, size_t len,
                       const struct body *b, const char *text) {
    size_t padding = __format_field_padding(sp->width, len + (sign ? 1 : 0));
    int zeros = (sp->flags & FLAG_ZERO) && !(sp->flags & FLAG_LEFT) && b != NULL;
    if (!(sp->flags & FLAG_LEFT) && !zeros) {
        __format_pad(o, ' ', padding);
    }
    if (sign) {
        __format_emit(o, &sign, 1);
    }
    int prefix = 1;
    if (zeros) {
        if (b->style == 'a') {
            __format_emit(o, b->upper ? "0X" : "0x", 2);
            prefix = 0;
        }
        __format_pad(o, '0', padding);
    }
    if (b != NULL) {
        emit_body(o, b, prefix);
    } else {
        __format_emit(o, text, len);
    }
    if (sp->flags & FLAG_LEFT) {
        __format_pad(o, ' ', padding);
    }
}

// %f with `precision` fraction digits.
static void fixed_body(struct body *b, struct rounded *r, const struct decimal *d, int precision,
                       int alt) {
    round_at(r, d, d->point + precision);
    int start = lead_index(r);
    b->style = 'f';
    b->r = r;
    // The integer digits are the indices below the point, from the carried 1
    // when rounding went up past the first digit (9.96 -> 10.0).
    b->int_from = start;
    b->int_to = d->point > start ? d->point : start;
    b->int_zero = d->point <= start;
    b->frac_from = d->point;
    b->frac_to = d->point + precision;
    b->point = precision > 0 || alt;
}

// %e with `precision` digits after the first.
static void exp_body(struct body *b, struct rounded *r, const struct decimal *d, int precision,
                     int alt) {
    b->style = 'e';
    b->r = r;
    if (d->ndigits == 0) {
        round_at(r, d, 0);  // zero: all digits 0, exponent 0
        b->int_from = 0;
        b->exponent = 0;
    } else {
        round_at(r, d, precision + 1);
        b->int_from = lead_index(r);
        b->exponent = d->point - 1 - b->int_from;
    }
    b->int_to = b->int_from + 1;
    b->int_zero = 0;
    b->frac_from = b->int_to;
    b->frac_to = b->int_to + precision;
    b->point = precision > 0 || alt;
}

// %g without '#': no trailing zeros in the fraction, and no point without one.
static void strip_zeros(struct body *b) {
    while (b->frac_to > b->frac_from && rdigit(b->r, b->frac_to - 1) == 0) {
        b->frac_to--;
    }
    b->point = b->frac_to > b->frac_from;
}

// %a: 0x1.<hex fraction>p<exponent>, subnormals as 0x0.<fraction>p-1022. A
// precision rounds the fraction to nearest, ties to even, which can carry
// into the leading digit (0x1.f8p+0 at precision 1 is 0x2.0p+0).
static void hex_body(struct body *b, uint64_t bits, int precision, int alt) {
    unsigned exp_field = (unsigned)(bits >> 52) & 0x7ff;
    uint64_t frac = bits & ((1ull << 52) - 1);
    b->style = 'a';
    b->hex_lead = exp_field != 0 ? 1 : 0;
    b->exponent = exp_field != 0 ? (int)exp_field - 1023 : (frac != 0 ? -1022 : 0);
    if (precision == NO_PRECISION) {
        precision = 13;  // as many digits as the value needs
        while (precision > 0 && ((frac >> (4 * (13 - precision))) & 0xf) == 0) {
            precision--;
        }
    } else if (precision < 13) {
        // Round leading digit and fraction together, so a tie at precision 0
        // goes to the even leading digit (0x1.8p+0 is 0x2p+0).
        int drop = 4 * (13 - precision);
        uint64_t all = ((uint64_t)b->hex_lead << 52) | frac;
        uint64_t kept = all >> drop;
        uint64_t rest = all & ((1ull << drop) - 1);
        uint64_t half = 1ull << (drop - 1);
        if (rest > half || (rest == half && (kept & 1))) {
            kept++;
        }
        b->hex_lead = (unsigned)(kept >> (4 * precision));
        frac = (kept & ((1ull << (4 * precision)) - 1)) << drop;
    }
    b->hex_frac = frac << 12;  // the 13 fraction digits at the top of the word
    b->hex_digits = precision;
    b->point = precision > 0 || alt;
}

void __format_float(struct out *o, const struct spec *sp, double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    unsigned exp_field = (unsigned)(bits >> 52) & 0x7ff;
    uint64_t frac = bits & ((1ull << 52) - 1);
    char conv = sp->conv;
    int upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
    char style = (char)(upper ? conv - 'A' + 'a' : conv);
    int alt = (sp->flags & FLAG_ALT) != 0;
    char sign = (bits >> 63) ? '-' : (sp->flags & FLAG_PLUS) ? '+' : (sp->flags & FLAG_SPACE) ? ' ' : 0;

    if (exp_field == 0x7ff) {
        const char *text = frac != 0 ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        emit_field(o, sp, sign, 3, NULL, text);
        return;
    }

    struct body b;
    memset(&b, 0, sizeof b);
    b.upper = upper;
    if (style == 'a') {
        hex_body(&b, bits, sp->precision, alt);
        emit_field(o, sp, sign, body_len(&b), &b, NULL);
        return;
    }

    // m * 2^e; subnormals have no implicit bit and the smallest exponent.
    uint64_t m = exp_field != 0 ? frac | (1ull << 52) : frac;
    int e = (exp_field != 0 ? (int)exp_field : 1) - 1075;
    struct decimal d;
    expand(&d, m, e);
    struct rounded r;
    int precision = sp->precision == NO_PRECISION ? 6 : sp->precision;

    if (style == 'f') {
        fixed_body(&b, &r, &d, precision, alt);
    } else if (style == 'e') {
        exp_body(&b, &r, &d, precision, alt);
    } else {
        // %g: P significant digits. With X the exponent %e would print, %f
        // style with P - 1 - X fraction digits if P > X >= -4, else %e style
        // with P - 1.
        int p = precision == 0 ? 1 : precision;
        int x = 0;
        if (d.ndigits != 0) {
            round_at(&r, &d, p);
            x = d.point - 1 - lead_index(&r);
        }
        if (x < p && x >= -4) {
            fixed_body(&b, &r, &d, p - 1 - x, alt);
        } else {
            exp_body(&b, &r, &d, p - 1, alt);
        }
        if (!alt) {
            strip_zeros(&b);
        }
    }
    emit_field(o, sp, sign, body_len(&b), &b, NULL);
}
