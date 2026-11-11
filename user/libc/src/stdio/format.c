// SPDX-License-Identifier: GPL-3.0-or-later
//
// The printf engine (see src/internal/format.h): C11 7.21.6.1 conversions
// d i o u x X c s p n %, the flags - + space # 0, field width and precision
// (both may be *), and the length modifiers hh h l ll j z t L. %lc and %ls
// write UTF-8, the libc's only multibyte encoding.
//
// The floating-point conversions (a A e E f F g G) are in format_float.c.
//
// Choices where the standard leaves the behaviour open: a null %s prints
// "(null)", %p prints like %#lx but always with the 0x prefix ("0x0" for a
// null pointer), and an unknown conversion is printed as written.

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "binary128.h"
#include "format_impl.h"

void __format_emit(struct out *o, const char *s, size_t n) {
    if (o->error || n == 0) {
        return;
    }
    if (n > (size_t)INT_MAX - o->count) {
        o->error = EOVERFLOW;
        return;
    }
    o->put(o->ctx, s, n);
    o->count += n;
}

void __format_pad(struct out *o, char c, size_t n) {
    char chunk[32];
    if (n > (size_t)INT_MAX - o->count) {
        o->error = EOVERFLOW;  // before writing two billion spaces
        return;
    }
    memset(chunk, c, n < sizeof chunk ? n : sizeof chunk);
    while (n > 0 && !o->error) {
        size_t k = n < sizeof chunk ? n : sizeof chunk;
        __format_emit(o, chunk, k);
        n -= k;
    }
}

#define emit __format_emit
#define pad __format_pad
#define field_padding __format_field_padding

static void format_integer(struct out *o, const struct spec *sp, uintmax_t value, int negative) {
    int base = sp->conv == 'o' ? 8 : (sp->conv == 'x' || sp->conv == 'X' || sp->conv == 'p') ? 16 : 10;
    const char *digit_chars = sp->conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
    char digits[24];  // 2^64 in octal is 22 digits
    size_t ndigits = 0;
    int precision = sp->precision;

    // With a precision of 0, the value 0 has no digits at all.
    if (!(value == 0 && precision == 0)) {
        do {
            digits[sizeof digits - 1 - ndigits++] = digit_chars[value % (unsigned)base];
            value /= (unsigned)base;
        } while (value != 0);
    }
    const char *first = digits + sizeof digits - ndigits;

    char prefix[2];
    size_t nprefix = 0;
    if (sp->conv == 'd' || sp->conv == 'i') {
        if (negative) {
            prefix[nprefix++] = '-';
        } else if (sp->flags & FLAG_PLUS) {
            prefix[nprefix++] = '+';
        } else if (sp->flags & FLAG_SPACE) {
            prefix[nprefix++] = ' ';
        }
    } else if (sp->conv == 'p' || ((sp->flags & FLAG_ALT) && (sp->conv == 'x' || sp->conv == 'X') &&
                                   ndigits > 0 && !(ndigits == 1 && first[0] == '0'))) {
        prefix[nprefix++] = '0';
        prefix[nprefix++] = sp->conv == 'X' ? 'X' : 'x';
    } else if ((sp->flags & FLAG_ALT) && sp->conv == 'o' && (ndigits == 0 || first[0] != '0')) {
        // '#' with o: the precision grows so that the first digit is a 0.
        if (precision == NO_PRECISION || (size_t)precision <= ndigits) {
            precision = (int)ndigits + 1;
        }
    }

    size_t zeros = precision != NO_PRECISION && (size_t)precision > ndigits
                       ? (size_t)precision - ndigits
                       : 0;
    // '0' pads to the width with zeros, unless '-' or a precision is given.
    if ((sp->flags & FLAG_ZERO) && !(sp->flags & FLAG_LEFT) && sp->precision == NO_PRECISION) {
        size_t z = field_padding(sp->width, nprefix + ndigits);
        zeros = z > zeros ? z : zeros;
    }
    size_t spaces = field_padding(sp->width, nprefix + zeros + ndigits);

    if (!(sp->flags & FLAG_LEFT)) {
        pad(o, ' ', spaces);
    }
    emit(o, prefix, nprefix);
    pad(o, '0', zeros);
    emit(o, first, ndigits);
    if (sp->flags & FLAG_LEFT) {
        pad(o, ' ', spaces);
    }
}

// UTF-8 encoding of one code point into out (4 bytes); 0 when it has none
// (a surrogate, or past U+10FFFF).
static size_t utf8_encode(unsigned long c, char *out) {
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xc0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 0x3f));
        return 2;
    }
    if (c >= 0xd800 && c <= 0xdfff) {
        return 0;
    }
    if (c < 0x10000) {
        out[0] = (char)(0xe0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[2] = (char)(0x80 | (c & 0x3f));
        return 3;
    }
    if (c <= 0x10ffff) {
        out[0] = (char)(0xf0 | (c >> 18));
        out[1] = (char)(0x80 | ((c >> 12) & 0x3f));
        out[2] = (char)(0x80 | ((c >> 6) & 0x3f));
        out[3] = (char)(0x80 | (c & 0x3f));
        return 4;
    }
    return 0;
}

static void format_bytes(struct out *o, const struct spec *sp, const char *s, size_t len) {
    size_t spaces = field_padding(sp->width, len);
    if (!(sp->flags & FLAG_LEFT)) {
        pad(o, ' ', spaces);
    }
    emit(o, s, len);
    if (sp->flags & FLAG_LEFT) {
        pad(o, ' ', spaces);
    }
}

static void format_string(struct out *o, const struct spec *sp, const char *s) {
    if (s == NULL) {
        s = "(null)";
    }
    size_t len = 0;
    // With a precision, the array need not be NUL terminated.
    while ((sp->precision == NO_PRECISION || len < (size_t)sp->precision) && s[len] != '\0') {
        len++;
    }
    format_bytes(o, sp, s, len);
}

// %ls: the precision limits the bytes written, never splitting a character.
static void format_wide_string(struct out *o, const struct spec *sp, const __WCHAR_TYPE__ *ws) {
    if (ws == NULL) {
        format_string(o, sp, NULL);
        return;
    }
    char enc[4];
    size_t len = 0, count = 0;
    for (; ws[count] != 0; count++) {
        size_t n = utf8_encode((unsigned long)ws[count], enc);
        if (n == 0) {
            o->error = EILSEQ;
            return;
        }
        if (sp->precision != NO_PRECISION && len + n > (size_t)sp->precision) {
            break;
        }
        len += n;
    }
    size_t spaces = field_padding(sp->width, len);
    if (!(sp->flags & FLAG_LEFT)) {
        pad(o, ' ', spaces);
    }
    for (size_t i = 0; i < count; i++) {
        emit(o, enc, utf8_encode((unsigned long)ws[i], enc));
    }
    if (sp->flags & FLAG_LEFT) {
        pad(o, ' ', spaces);
    }
}

static intmax_t signed_arg(enum length length, va_list *ap) {
    switch (length) {
    case LEN_HH: return (signed char)va_arg(*ap, int);
    case LEN_H: return (short)va_arg(*ap, int);
    case LEN_L: return va_arg(*ap, long);
    case LEN_LL: return va_arg(*ap, long long);
    case LEN_J: return va_arg(*ap, intmax_t);
    case LEN_Z: return (intmax_t)(ptrdiff_t)va_arg(*ap, size_t);
    case LEN_T: return va_arg(*ap, ptrdiff_t);
    default: return va_arg(*ap, int);
    }
}

static uintmax_t unsigned_arg(enum length length, va_list *ap) {
    switch (length) {
    case LEN_HH: return (unsigned char)va_arg(*ap, unsigned int);
    case LEN_H: return (unsigned short)va_arg(*ap, unsigned int);
    case LEN_L: return va_arg(*ap, unsigned long);
    case LEN_LL: return va_arg(*ap, unsigned long long);
    case LEN_J: return va_arg(*ap, uintmax_t);
    case LEN_Z: return va_arg(*ap, size_t);
    case LEN_T: return (uintmax_t)(size_t)va_arg(*ap, ptrdiff_t);
    default: return va_arg(*ap, unsigned int);
    }
}

static void store_count(enum length length, size_t count, va_list *ap) {
    switch (length) {
    case LEN_HH: *va_arg(*ap, signed char *) = (signed char)count; break;
    case LEN_H: *va_arg(*ap, short *) = (short)count; break;
    case LEN_L: *va_arg(*ap, long *) = (long)count; break;
    case LEN_LL: *va_arg(*ap, long long *) = (long long)count; break;
    case LEN_J: *va_arg(*ap, intmax_t *) = (intmax_t)count; break;
    case LEN_Z: *va_arg(*ap, size_t *) = count; break;
    case LEN_T: *va_arg(*ap, ptrdiff_t *) = (ptrdiff_t)count; break;
    default: *va_arg(*ap, int *) = (int)count; break;
    }
}

// Reads a decimal field (width or precision); -1 if it does not fit an int.
// A long double argument, narrowed to double: AArch64's binary128 cannot be
// computed with (no compiler-rt), so its bits are converted by hand.
static double long_double_arg(va_list *ap) {
    long double v = va_arg(*ap, long double);
#if defined(__aarch64__) && __LDBL_MANT_DIG__ == 113
    unsigned long long words[2];
    memcpy(words, &v, sizeof words);
    return __binary128_to_double(words);
#else
    return (double)v;
#endif
}

static int read_number(const char **p) {
    int n = 0;
    while (**p >= '0' && **p <= '9') {
        int d = *(*p)++ - '0';
        if (n > (INT_MAX - d) / 10) {
            n = -1;
            while (**p >= '0' && **p <= '9') {
                (*p)++;
            }
            break;
        }
        n = n * 10 + d;
    }
    return n;
}

int __format(format_put_fn put, void *ctx, const char *fmt, va_list args) {
    struct out o = {put, ctx, 0, 0};
    va_list ap;
    va_copy(ap, args);  // through a pointer below, whatever va_list's type is

    while (*fmt != '\0' && !o.error) {
        if (*fmt != '%') {
            const char *run = fmt;
            while (*fmt != '\0' && *fmt != '%') {
                fmt++;
            }
            emit(&o, run, (size_t)(fmt - run));
            continue;
        }
        const char *start = fmt++;
        struct spec sp = {0, 0, NO_PRECISION, LEN_NONE, 0};

        for (;; fmt++) {
            if (*fmt == '-') {
                sp.flags |= FLAG_LEFT;
            } else if (*fmt == '+') {
                sp.flags |= FLAG_PLUS;
            } else if (*fmt == ' ') {
                sp.flags |= FLAG_SPACE;
            } else if (*fmt == '#') {
                sp.flags |= FLAG_ALT;
            } else if (*fmt == '0') {
                sp.flags |= FLAG_ZERO;
            } else {
                break;
            }
        }
        if (*fmt == '*') {
            fmt++;
            int w = va_arg(ap, int);
            if (w == INT_MIN) {
                o.error = EOVERFLOW;
                continue;
            }
            if (w < 0) {
                sp.flags |= FLAG_LEFT;  // a negative width is '-' and its magnitude
                w = -w;
            }
            sp.width = w;
        } else if ((sp.width = read_number(&fmt)) < 0) {
            o.error = EOVERFLOW;
            continue;
        }
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                fmt++;
                int p = va_arg(ap, int);
                sp.precision = p < 0 ? NO_PRECISION : p;  // negative: as if absent
            } else if ((sp.precision = read_number(&fmt)) < 0) {  // "." alone is 0
                o.error = EOVERFLOW;
                continue;
            }
        }

        switch (*fmt) {
        case 'h':
            sp.length = fmt[1] == 'h' ? LEN_HH : LEN_H;
            fmt += sp.length == LEN_HH ? 2 : 1;
            break;
        case 'l':
            sp.length = fmt[1] == 'l' ? LEN_LL : LEN_L;
            fmt += sp.length == LEN_LL ? 2 : 1;
            break;
        case 'j': sp.length = LEN_J; fmt++; break;
        case 'z': sp.length = LEN_Z; fmt++; break;
        case 't': sp.length = LEN_T; fmt++; break;
        case 'L': sp.length = LEN_BIG_L; fmt++; break;
        default: break;
        }

        sp.conv = *fmt;
        if (sp.conv == '\0') {
            emit(&o, start, (size_t)(fmt - start));  // a lone % at the end
            break;
        }
        fmt++;
        switch (sp.conv) {
        case 'd':
        case 'i': {
            intmax_t v = signed_arg(sp.length, &ap);
            format_integer(&o, &sp, v < 0 ? 0 - (uintmax_t)v : (uintmax_t)v, v < 0);
            break;
        }
        case 'o':
        case 'u':
        case 'x':
        case 'X':
            format_integer(&o, &sp, unsigned_arg(sp.length, &ap), 0);
            break;
        case 'p':
            format_integer(&o, &sp, (uintptr_t)va_arg(ap, void *), 0);
            break;
        case 'c':
            if (sp.length == LEN_L) {
                char enc[4];
                size_t n = utf8_encode((unsigned long)(unsigned)va_arg(ap, __WINT_TYPE__), enc);
                if (n == 0) {
                    o.error = EILSEQ;
                } else {
                    format_bytes(&o, &sp, enc, n);
                }
            } else {
                char c = (char)va_arg(ap, int);
                format_bytes(&o, &sp, &c, 1);
            }
            break;
        case 's':
            if (sp.length == LEN_L) {
                format_wide_string(&o, &sp, va_arg(ap, const __WCHAR_TYPE__ *));
            } else {
                format_string(&o, &sp, va_arg(ap, const char *));
            }
            break;
        case 'n':
            store_count(sp.length, o.count, &ap);
            break;
        case '%':
            emit(&o, "%", 1);
            break;
        case 'a':
        case 'A':
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
            __format_float(&o, &sp, sp.length == LEN_BIG_L ? long_double_arg(&ap)
                                                           : va_arg(ap, double));
            break;
        default:
            emit(&o, start, (size_t)(fmt - start));  // unknown: print it as written
            break;
        }
    }
    va_end(ap);
    if (o.error) {
        errno = o.error;
        return -1;
    }
    return (int)o.count;
}
