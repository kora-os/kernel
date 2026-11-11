#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "binary128.h"

double atof(const char *nptr) {
    double result = 0.0;
    int sign = 1;
    while (isspace((unsigned char)*nptr)) {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (isdigit((unsigned char)*nptr)) {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double frac = 0.0;
        double base = 0.1;
        while (isdigit((unsigned char)*nptr)) {
            frac += (*nptr - '0') * base;
            base *= 0.1;
            nptr++;
        }
        result += frac;
    }
    return sign * result;
}

// atoi and friends are strtol in base 10 with no end pointer. Out-of-range
// input is undefined behaviour for them; here it clamps like strtol (and sets
// errno) before the conversion to the narrower type.
int atoi(const char *nptr) {
    return (int)strtol(nptr, NULL, 10);
}

long int atol(const char *nptr) {
    return strtol(nptr, NULL, 10);
}

long long int atoll(const char *nptr) {
    return strtoll(nptr, NULL, 10);
}

/*
We need snprintf for these


int strfromd(char *restrict s, size_t n, const char *restrict format, double fp) {
    return snprintf(s, n, format, fp);
}

int strfromf(char *restrict s, size_t n, const char *restrict format, float fp) {
    return snprintf(s, n, format, fp);
}

int strfroml(char *restrict s, size_t n, const char *restrict format, long double fp) {
    return snprintf(s, n, format, fp);
}
*/

double strtod(const char *nptr, char **endptr) {
    double result = 0.0;
    int sign = 1;
    while (isspace((unsigned char)*nptr)) {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (isdigit((unsigned char)*nptr)) {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double frac = 0.0;
        double base = 0.1;
        while (isdigit((unsigned char)*nptr)) {
            frac += (*nptr - '0') * base;
            base *= 0.1;
            nptr++;
        }
        result += frac;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return sign * result;
}

float strtof(const char *nptr, char **endptr) {
    double result = 0.0;
    int sign = 1;
    while (isspace((unsigned char)*nptr)) {
        nptr++;
    }
    if (*nptr == '-') {
        sign = -1;
        nptr++;
    } else if (*nptr == '+') {
        nptr++;
    }
    while (isdigit((unsigned char)*nptr)) {
        result = result * 10 + (*nptr - '0');
        nptr++;
    }
    if (*nptr == '.') {
        nptr++;
        double frac = 0.0;
        double base = 0.1;
        while (isdigit((unsigned char)*nptr)) {
            frac += (*nptr - '0') * base;
            base *= 0.1;
            nptr++;
        }
        result += frac;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return (float)(sign * result);
}

// Parsed as a double and widened: no binary128 arithmetic (see binary128.h),
// so strtold is exactly as precise as strtod.
long double strtold(const char *nptr, char **endptr) {
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

// The integer strto* functions share one parser. It accepts optional white
// space, an optional sign and digits of `base` (2 to 36; 0 picks 16 for a 0x
// prefix, 8 for a leading 0, 10 otherwise; 16 also allows the 0x prefix). It
// returns the magnitude, sets *negative and *overflow (the magnitude exceeded
// `limit`, in which case it returns `limit`), and leaves *endptr after the
// last digit, or at nptr itself when there are no digits. Every digit is
// consumed even past an overflow.
static int digit_value(unsigned char ch) {
    if (isdigit(ch)) {
        return ch - '0';
    }
    if (isalpha(ch)) {
        return tolower(ch) - 'a' + 10;
    }
    return 99;  // larger than any base
}

static unsigned long long parse_integer(const char *nptr, char **endptr, int base,
                                        unsigned long long limit, int *negative,
                                        int *overflow) {
    const char *s = nptr;
    *negative = 0;
    *overflow = 0;
    if (base < 0 || base == 1 || base > 36) {
        errno = EINVAL;
        if (endptr) {
            *endptr = (char *)nptr;
        }
        return 0;
    }
    while (isspace((unsigned char)*s)) {
        s++;
    }
    if (*s == '-' || *s == '+') {
        *negative = *s == '-';
        s++;
    }
    // A 0x prefix counts only when a hex digit follows it: "0x" alone is the
    // number 0 followed by "x".
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
        digit_value((unsigned char)s[2]) < 16) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = s[0] == '0' ? 8 : 10;
    }

    const char *digits = s;
    unsigned long long value = 0;
    for (;; s++) {
        int d = digit_value((unsigned char)*s);
        if (d >= base) {
            break;
        }
        if (*overflow) {
            continue;
        }
        if (value > (limit - (unsigned long long)d) / (unsigned long long)base) {
            *overflow = 1;
            value = limit;
        } else {
            value = value * (unsigned long long)base + (unsigned long long)d;
        }
    }
    if (endptr) {
        *endptr = (char *)(s == digits ? nptr : s);
    }
    return value;
}

// A signed conversion with range [-max - 1, max].
static long long parse_signed(const char *nptr, char **endptr, int base, long long max) {
    int negative, overflow;
    unsigned long long limit = (unsigned long long)max + 1;  // the magnitude of min
    unsigned long long value = parse_integer(nptr, endptr, base, limit, &negative, &overflow);
    if (!negative && value > (unsigned long long)max) {
        errno = ERANGE;
        return max;
    }
    if (overflow) {
        errno = ERANGE;
        return -max - 1;
    }
    if (!negative || value == 0) {
        return (long long)value;
    }
    // 0 < value <= limit here: negate without overflowing on the most
    // negative value, whose magnitude does not fit in long long.
    return -(long long)(value - 1) - 1;
}

// An unsigned conversion with range [0, max]. As the standard requires, a
// minus sign negates the result in the unsigned type ("-1" is max).
static unsigned long long parse_unsigned(const char *nptr, char **endptr, int base,
                                         unsigned long long max) {
    int negative, overflow;
    unsigned long long value = parse_integer(nptr, endptr, base, max, &negative, &overflow);
    if (overflow) {
        errno = ERANGE;
        return max;
    }
    return negative ? (max - value + 1) & max : value;
}

long int strtol(const char *restrict nptr, char **restrict endptr, int base) {
    return (long)parse_signed(nptr, endptr, base, LONG_MAX);
}

long long int strtoll(const char *restrict nptr, char **restrict endptr, int base) {
    return parse_signed(nptr, endptr, base, LLONG_MAX);
}

unsigned long int strtoul(const char *restrict nptr, char **restrict endptr, int base) {
    return (unsigned long)parse_unsigned(nptr, endptr, base, ULONG_MAX);
}

unsigned long long int strtoull(const char *restrict nptr, char **restrict endptr, int base) {
    return parse_unsigned(nptr, endptr, base, ULLONG_MAX);
}
