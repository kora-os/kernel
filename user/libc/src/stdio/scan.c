// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scanf engine (C11 7.21.6.2).
//
// An input item is the longest run of characters, within the field width,
// that is a number of the wanted kind or the beginning of one. If the run is
// not a whole number ("0x" with no digit after it, "1e+", "infin") the
// conversion fails, and the characters read stay read: a stream takes back
// one character only, and that is how the standard defines it. (strtod and
// strtol, working on a string, back up instead: strtod("1e+", ...) is 1.)
//
// A conversion fails in one of two ways. A matching failure: the input does
// not fit; scanning stops and the count so far is returned. An input
// failure: the input ended first; EOF is returned if nothing was assigned.
//
// Integers are accumulated as they are read and clamp like strtol. Floats
// go to strtod, strtof or strtold, so they are correctly rounded: the digits
// are copied into a text that strtod reads to the same bits as the original,
// but of bounded length (see collect_float). That text and strtod's own
// working space make a float conversion the deep end of the stack, about
// 1.5 KB of a main thread's 4 KB.
//
// Not supported: the wide-character conversions %lc, %ls and %l[ (there is
// no <wchar.h>); they stop the scan as a matching failure.

#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "scan.h"

#define STOP (-2)  // the field width is used up (EOF is -1)

enum outcome { INPUT_FAILURE = -1, MATCHING_FAILURE = 0, MATCHED = 1 };

enum length { LEN_HH, LEN_H, LEN_NONE, LEN_L, LEN_LL, LEN_J, LEN_Z, LEN_T, LEN_BIG_L };

struct state {
    struct scan_input *in;
    long count;  // characters consumed so far, for %n
};

static int next(struct state *s) {
    int c = s->in->get(s->in);
    if (c != EOF) {
        s->count++;
    }
    return c;
}

static void back(struct state *s, int c) {
    if (c != EOF) {
        s->in->unget(s->in, c);
        s->count--;
    }
}

// The next character of an input item: STOP once the field width is used up.
static int item_next(struct state *s, long *width) {
    if (*width == 0) {
        return STOP;
    }
    int c = next(s);
    if (c != EOF) {
        (*width)--;
    }
    return c;
}

// Gives back the character that ended an item (nothing for EOF or STOP).
static void item_back(struct state *s, int c) {
    if (c >= 0) {
        back(s, c);
    }
}

// An item that is not a whole number: the input ending is an input failure,
// anything else (a character that does not fit, the width) a matching one.
static enum outcome failed_at(int c) {
    return c == EOF ? INPUT_FAILURE : MATCHING_FAILURE;
}

static void skip_space(struct state *s) {
    int c;
    do {
        c = next(s);
    } while (c != EOF && isspace(c));
    back(s, c);
}

static int digit_value(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;  // no digit in any base (EOF and STOP too)
}

// An integer in `base` (0: decided by the prefix, as for %i), with the
// optional sign and "0x" that strtol accepts.
static enum outcome scan_integer(struct state *s, long width, int base, int is_signed,
                                 unsigned long long *out) {
    int c = item_next(s, &width);
    int negative = 0, digits = 0;
    if (c == '+' || c == '-') {
        negative = c == '-';
        c = item_next(s, &width);
    }
    if (c == '0') {
        digits = 1;  // a number already, unless an x follows
        c = item_next(s, &width);
        if ((c == 'x' || c == 'X') && (base == 0 || base == 16)) {
            base = 16;
            digits = 0;  // "0x" needs a digit after it
            c = item_next(s, &width);
        } else if (base == 0) {
            base = 8;
        }
    } else if (base == 0) {
        base = 10;
    }
    unsigned long long value = 0;
    int overflow = 0;
    for (;; c = item_next(s, &width)) {
        int v = digit_value(c);
        if (v >= base) {
            break;
        }
        digits = 1;
        if (value > (ULLONG_MAX - (unsigned)v) / (unsigned)base) {
            overflow = 1;
        } else {
            value = value * (unsigned)base + (unsigned)v;
        }
    }
    item_back(s, c);
    if (!digits) {
        return failed_at(c);
    }
    if (is_signed) {
        unsigned long long limit = negative ? (unsigned long long)LLONG_MAX + 1 : LLONG_MAX;
        if (overflow || value > limit) {
            value = limit;
        }
    } else if (overflow) {
        *out = ULLONG_MAX;
        return MATCHED;
    }
    *out = negative ? 0 - value : value;
    return MATCHED;
}

static void store_integer(void *p, enum length length, unsigned long long v) {
    switch (length) {
    case LEN_HH:
        *(unsigned char *)p = (unsigned char)v;
        break;
    case LEN_H:
        *(unsigned short *)p = (unsigned short)v;
        break;
    case LEN_NONE:
        *(unsigned int *)p = (unsigned int)v;
        break;
    case LEN_L:
        *(unsigned long *)p = (unsigned long)v;
        break;
    case LEN_J:
        *(uintmax_t *)p = (uintmax_t)v;
        break;
    case LEN_Z:
        *(size_t *)p = (size_t)v;
        break;
    case LEN_T:
        *(ptrdiff_t *)p = (ptrdiff_t)v;
        break;
    default:  // ll (and L, which is not defined for integers)
        *(unsigned long long *)p = v;
        break;
    }
}

// The significant digits strtod keeps exactly; it only asks of any later
// digit whether it is zero (MAX_DIGITS and the hexadecimal 16 in
// src/stdlib/strtod.c).
#define DECIMAL_DIGITS 800
#define HEX_DIGITS 16
// A sign, "0x", the digits, one more for "and something nonzero after", an
// exponent and the NUL.
#define FLOAT_TEXT (1 + 2 + DECIMAL_DIGITS + 1 + 24 + 1)

// Reads the rest of a word (`rest`, lower case) without regard to case.
// MATCHED, or how it failed, with the character that did not fit given back.
static enum outcome match_word(struct state *s, long *width, const char *rest) {
    for (; *rest; rest++) {
        int c = item_next(s, width);
        if (c < 0 || tolower(c) != *rest) {
            item_back(s, c);
            return failed_at(c);
        }
    }
    return MATCHED;
}

// Reads a floating-point input item and writes into `text` a number strtod
// converts to the same bits. The item may hold any number of digits, and a
// main thread's stack is 4 KB, so the text is not a copy: leading zeros are
// dropped, digits beyond those strtod keeps become a single 1 if any of them
// is nonzero, the point is removed and the exponent adjusted to match.
static enum outcome collect_float(struct state *s, long width, char *text) {
    char *o = text;
    int c = item_next(s, &width);
    if (c == '+' || c == '-') {
        if (c == '-') {
            *o++ = '-';
        }
        c = item_next(s, &width);
    }
    if (c == 'i' || c == 'I') {
        // "inf", or all of "infinity".
        enum outcome word = match_word(s, &width, "nf");
        if (word != MATCHED) {
            return word;
        }
        c = item_next(s, &width);
        if (c == 'i' || c == 'I') {
            word = match_word(s, &width, "nity");
            if (word != MATCHED) {
                return word;
            }
        } else {
            item_back(s, c);
        }
        *o++ = 'i', *o++ = 'n', *o++ = 'f', *o = '\0';
        return MATCHED;
    }
    if (c == 'n' || c == 'N') {
        // "nan", or "nan(" letters, digits and underscores ")".
        enum outcome word = match_word(s, &width, "an");
        if (word != MATCHED) {
            return word;
        }
        c = item_next(s, &width);
        if (c == '(') {
            do {
                c = item_next(s, &width);
            } while (c >= 0 && (isalnum(c) || c == '_'));
            if (c != ')') {
                item_back(s, c);
                return failed_at(c);
            }
        } else {
            item_back(s, c);
        }
        *o++ = 'n', *o++ = 'a', *o++ = 'n', *o = '\0';
        return MATCHED;
    }

    int hex = 0, digits = 0;
    if (c == '0') {
        digits = 1;
        c = item_next(s, &width);
        if (c == 'x' || c == 'X') {
            hex = 1;
            digits = 0;  // "0x" needs a digit after it
            *o++ = '0', *o++ = 'x';
            c = item_next(s, &width);
        }
    }
    int base = hex ? 16 : 10;
    int kept = 0, limit = hex ? HEX_DIGITS : DECIMAL_DIGITS;
    int step = hex ? 4 : 1;  // what one digit position is worth in the exponent
    int more = 0, seen_point = 0;
    long exponent = 0;
    for (;; c = item_next(s, &width)) {
        if (c == '.' && !seen_point) {
            seen_point = 1;
            continue;
        }
        int v = digit_value(c);
        if (v >= base) {
            break;
        }
        digits = 1;
        if (kept == 0 && v == 0) {
            if (seen_point) {
                exponent -= step;  // a leading zero after the point
            }
        } else if (kept < limit) {
            *o++ = (char)c;
            kept++;
            if (seen_point) {
                exponent -= step;
            }
        } else {
            more |= v != 0;
            if (!seen_point) {
                exponent += step;
            }
        }
    }
    if (!digits) {
        item_back(s, c);
        return failed_at(c);
    }
    if (kept == 0) {
        *o++ = '0';
        exponent = 0;
    } else if (more) {
        *o++ = '1';  // one digit place further down
        exponent -= step;
    }
    if (c == (hex ? 'p' : 'e') || c == (hex ? 'P' : 'E')) {
        // An exponent, which needs at least one digit.
        int negative = 0;
        c = item_next(s, &width);
        if (c == '+' || c == '-') {
            negative = c == '-';
            c = item_next(s, &width);
        }
        if (digit_value(c) >= 10) {
            item_back(s, c);
            return failed_at(c);
        }
        long x = 0;
        for (; digit_value(c) < 10; c = item_next(s, &width)) {
            if (x < 100000) {  // far beyond any format's range already
                x = x * 10 + (c - '0');
            }
        }
        if (kept != 0) {
            exponent += negative ? -x : x;
        }
    }
    item_back(s, c);

    *o++ = hex ? 'p' : 'e';
    if (exponent < 0) {
        *o++ = '-';
        exponent = -exponent;
    }
    char reversed[24];
    int n = 0;
    do {
        reversed[n++] = (char)('0' + exponent % 10);
        exponent /= 10;
    } while (exponent != 0);
    while (n > 0) {
        *o++ = reversed[--n];
    }
    *o = '\0';
    return MATCHED;
}

// Kept out of line: its text only takes stack while a float is converted.
__attribute__((noinline)) static enum outcome scan_float(struct state *s, long width,
                                                         enum length length, void *p) {
    char text[FLOAT_TEXT];
    enum outcome outcome = collect_float(s, width, text);
    if (outcome != MATCHED || p == NULL) {
        return outcome;
    }
    if (length == LEN_BIG_L) {
        *(long double *)p = strtold(text, NULL);
    } else if (length == LEN_L) {
        *(double *)p = strtod(text, NULL);
    } else {
        *(float *)p = strtof(text, NULL);
    }
    return MATCHED;
}

// Whether c belongs to the scan set [set, end): single characters and
// ranges "a-z". A '-' at either end is a plain character.
static int in_set(const unsigned char *set, const unsigned char *end, int c) {
    for (const unsigned char *p = set; p < end; p++) {
        if (p + 2 < end && p[1] == '-') {
            if (c >= p[0] && c <= p[2]) {
                return 1;
            }
            p += 2;
        } else if (c == *p) {
            return 1;
        }
    }
    return 0;
}

int __scan(struct scan_input *in, const char *format, va_list ap) {
    struct state s = {in, 0};
    const unsigned char *f = (const unsigned char *)format;
    int assigned = 0;

    while (*f != '\0') {
        if (isspace(*f)) {
            // White space in the format: any amount in the input, even none.
            do {
                f++;
            } while (isspace(*f));
            skip_space(&s);
            continue;
        }
        if (*f != '%' || f[1] == '%') {
            // An ordinary character, or %% (which skips white space first).
            if (*f == '%') {
                f++;
                skip_space(&s);
            }
            int c = next(&s);
            if (c == EOF) {
                goto input_failure;
            }
            if (c != *f) {
                back(&s, c);
                return assigned;
            }
            f++;
            continue;
        }

        // A conversion: % [*] [width] [length] specifier.
        f++;
        int suppress = 0;
        if (*f == '*') {
            suppress = 1;
            f++;
        }
        long width = 0;
        while (isdigit(*f)) {
            if (width < LONG_MAX / 10 - 1) {
                width = width * 10 + (*f - '0');
            }
            f++;
        }
        enum length length = LEN_NONE;
        switch (*f) {
        case 'h':
            length = *++f == 'h' ? (f++, LEN_HH) : LEN_H;
            break;
        case 'l':
            length = *++f == 'l' ? (f++, LEN_LL) : LEN_L;
            break;
        case 'j':
            length = LEN_J, f++;
            break;
        case 'z':
            length = LEN_Z, f++;
            break;
        case 't':
            length = LEN_T, f++;
            break;
        case 'L':
            length = LEN_BIG_L, f++;
            break;
        default:
            break;
        }
        int conv = *f;
        if (conv == '\0') {
            return assigned;  // the format ends inside a conversion
        }
        f++;

        if (conv == 'n') {
            if (!suppress) {
                store_integer(va_arg(ap, void *), length, (unsigned long long)s.count);
            }
            continue;
        }
        if ((conv == 'c' || conv == 's' || conv == '[') && length == LEN_L) {
            return assigned;  // wide characters: not supported
        }

        enum outcome outcome;
        switch (conv) {
        case 'c': {
            // Exactly `width` characters (1 by default), white space included.
            char *dest = suppress ? NULL : va_arg(ap, char *);
            long want = width > 0 ? width : 1, got = 0;
            int c;
            while (got < want && (c = next(&s)) != EOF) {
                if (dest != NULL) {
                    dest[got] = (char)c;
                }
                got++;
            }
            // Fewer than that can only mean the input ended.
            outcome = got == want ? MATCHED : INPUT_FAILURE;
            break;
        }
        case 's':
        case '[': {
            const unsigned char *set = NULL, *set_end = NULL;
            int negated = 0;
            if (conv == '[') {
                if (*f == '^') {
                    negated = 1;
                    f++;
                }
                set = f;
                if (*f == ']') {
                    f++;  // a ] first is a member of the set
                }
                while (*f != '\0' && *f != ']') {
                    f++;
                }
                if (*f == '\0') {
                    return assigned;  // no closing ]
                }
                set_end = f++;
            } else {
                skip_space(&s);
            }
            char *dest = suppress ? NULL : va_arg(ap, char *);
            long room = width > 0 ? width : LONG_MAX, got = 0;
            int c;
            for (;;) {
                c = item_next(&s, &room);
                if (c < 0) {
                    break;
                }
                int wanted = conv == 's' ? !isspace(c) : in_set(set, set_end, c) != negated;
                if (!wanted) {
                    break;
                }
                if (dest != NULL) {
                    dest[got] = (char)c;
                }
                got++;
            }
            item_back(&s, c);
            if (got == 0) {
                outcome = failed_at(c);
                break;
            }
            if (dest != NULL) {
                dest[got] = '\0';
            }
            outcome = MATCHED;
            break;
        }
        case 'd':
        case 'i':
        case 'o':
        case 'u':
        case 'x':
        case 'X':
        case 'p': {
            int base = conv == 'd' || conv == 'u' ? 10 : conv == 'o' ? 8 : conv == 'i' ? 0 : 16;
            unsigned long long value = 0;
            skip_space(&s);
            outcome = scan_integer(&s, width > 0 ? width : LONG_MAX, base,
                                   conv == 'd' || conv == 'i', &value);
            if (outcome == MATCHED && !suppress) {
                void *p = va_arg(ap, void *);
                if (conv == 'p') {
                    *(void **)p = (void *)(uintptr_t)value;
                } else {
                    store_integer(p, length, value);
                }
            }
            break;
        }
        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G': {
            skip_space(&s);
            void *p = suppress ? NULL : va_arg(ap, void *);
            outcome = scan_float(&s, width > 0 ? width : LONG_MAX, length, p);
            break;
        }
        default:
            return assigned;  // not a conversion
        }

        if (outcome == INPUT_FAILURE) {
            goto input_failure;
        }
        if (outcome == MATCHING_FAILURE) {
            return assigned;
        }
        if (!suppress) {
            assigned++;
        }
    }
    return assigned;

input_failure:
    return assigned > 0 ? assigned : EOF;
}
