#include <stdlib.h>
#include <ctype.h>

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

int atoi(const char *nptr) {
    int result = 0;
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
    return sign * result;
}

long int atol(const char *nptr) {
    long int result = 0;
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
    return sign * result;
}

long long int atoll(const char *nptr) {
    long long int result = 0;
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
    return sign * result;
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

long int strtol(const char *nptr, char **endptr, int base) {
    long int result = 0;
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
    if (base == 0) {
        if (*nptr == '0') {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X') {
                base = 16;
                nptr++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    }
    while (*nptr) {
        int digit;
        unsigned char ch = (unsigned char)*nptr;
        if (isdigit(ch)) {
            digit = *nptr - '0';
        } else if (isalpha(ch)) {
            digit = tolower(ch) - 'a' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        nptr++;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return sign * result;
}

long long int strtoll(const char *nptr, char **endptr, int base) {
    long long int result = 0;
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
    if (base == 0) {
        if (*nptr == '0') {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X') {
                base = 16;
                nptr++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    }
    while (*nptr) {
        int digit;
        unsigned char ch = (unsigned char)*nptr;
        if (isdigit(ch)) {
            digit = *nptr - '0';
        } else if (isalpha(ch)) {
            digit = tolower(ch) - 'a' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        nptr++;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return sign * result;
}

unsigned long int strtoul(const char *nptr, char **endptr, int base) {
    unsigned long int result = 0;
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
    if (base == 0) {
        if (*nptr == '0') {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X') {
                base = 16;
                nptr++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    }
    while (*nptr) {
        int digit;
        unsigned char ch = (unsigned char)*nptr;
        if (isdigit(ch)) {
            digit = *nptr - '0';
        } else if (isalpha(ch)) {
            digit = tolower(ch) - 'a' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        nptr++;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return (unsigned long int)(sign * result);
}

unsigned long long int strtoull(const char *nptr, char **endptr, int base) {
    unsigned long long int result = 0;
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
    if (base == 0) {
        if (*nptr == '0') {
            nptr++;
            if (*nptr == 'x' || *nptr == 'X') {
                base = 16;
                nptr++;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    }
    while (*nptr) {
        int digit;
        unsigned char ch = (unsigned char)*nptr;
        if (isdigit(ch)) {
            digit = *nptr - '0';
        } else if (isalpha(ch)) {
            digit = tolower(ch) - 'a' + 10;
        } else {
            break;
        }
        if (digit >= base) {
            break;
        }
        result = result * base + digit;
        nptr++;
    }
    if (endptr) {
        *endptr = (char *)nptr;
    }
    return (unsigned long long int)(sign * result);
}
