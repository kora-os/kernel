// SPDX-License-Identifier: GPL-3.0-or-later
//
// The functions of <inttypes.h> (C11 7.8.2). intmax_t is long on AArch64, so
// strtoimax and strtoumax are the strtoll family under another name. The
// wide versions follow the same rules (src/stdlib/convert.c) on a wchar_t
// string: in the "C" locale only ASCII white space, signs, digits and letters
// take part, and the host tests check them against the narrow ones.

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>

intmax_t imaxabs(intmax_t j) {
    return j < 0 ? -j : j;
}

imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom) {
    imaxdiv_t r = {numer / denom, numer % denom};
    return r;
}

intmax_t strtoimax(const char *restrict nptr, char **restrict endptr, int base) {
    return (intmax_t)strtoll(nptr, endptr, base);
}

uintmax_t strtoumax(const char *restrict nptr, char **restrict endptr, int base) {
    return (uintmax_t)strtoull(nptr, endptr, base);
}

static int wide_digit(wchar_t c) {
    if (c >= L'0' && c <= L'9') {
        return (int)(c - L'0');
    }
    if (c >= L'a' && c <= L'z') {
        return (int)(c - L'a') + 10;
    }
    if (c >= L'A' && c <= L'Z') {
        return (int)(c - L'A') + 10;
    }
    return 99;  // larger than any base
}

static int wide_space(wchar_t c) {
    return c < 128 && isspace((int)c);
}

// As parse_integer in convert.c: the magnitude, clamped to `limit`, with the
// sign and whether it overflowed; *endptr after the last digit, or at nptr
// when there is none.
static uintmax_t parse_wide(const wchar_t *nptr, wchar_t **endptr, int base, uintmax_t limit,
                            int *negative, int *overflow) {
    const wchar_t *s = nptr;
    *negative = 0;
    *overflow = 0;
    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr) {
            *endptr = (wchar_t *)nptr;
        }
        return 0;
    }
    while (wide_space(*s)) {
        s++;
    }
    if (*s == L'-' || *s == L'+') {
        *negative = *s == L'-';
        s++;
    }
    if ((base == 0 || base == 16) && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X') &&
        wide_digit(s[2]) < 16) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == L'0' ? 8 : 10;
    }
    const wchar_t *digits = s;
    uintmax_t value = 0;
    for (;; s++) {
        int d = wide_digit(*s);
        if (d >= base) {
            break;
        }
        if (*overflow) {
            continue;
        }
        if (value > (limit - (uintmax_t)d) / (uintmax_t)base) {
            *overflow = 1;
            value = limit;
        } else {
            value = value * (uintmax_t)base + (uintmax_t)d;
        }
    }
    if (endptr) {
        *endptr = (wchar_t *)(s == digits ? nptr : s);
    }
    return value;
}

intmax_t wcstoimax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    int negative, overflow;
    uintmax_t limit = (uintmax_t)INTMAX_MAX + 1;  // the magnitude of INTMAX_MIN
    uintmax_t value = parse_wide(nptr, endptr, base, limit, &negative, &overflow);
    if (!negative && value > (uintmax_t)INTMAX_MAX) {
        errno = ERANGE;
        return INTMAX_MAX;
    }
    if (overflow) {
        errno = ERANGE;
        return INTMAX_MIN;
    }
    if (!negative || value == 0) {
        return (intmax_t)value;
    }
    return -(intmax_t)(value - 1) - 1;
}

uintmax_t wcstoumax(const wchar_t *restrict nptr, wchar_t **restrict endptr, int base) {
    int negative, overflow;
    uintmax_t value = parse_wide(nptr, endptr, base, UINTMAX_MAX, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return UINTMAX_MAX;
    }
    return negative ? 0 - value : value;
}
