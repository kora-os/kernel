// SPDX-License-Identifier: GPL-3.0-or-later
//
// The differential half of math_test.c, included once for double and once
// for float. The includer defines:
//
//   REAL, UINT, SINT     the type and integers of its size
//   SFX(name)            name, or name##f
//   SUFFIX               "" or "f", for the host's symbol and for messages
//   T(name)              a name of this instance: name##_d or name##_f
//   REAL_MAX, REAL_MIN, REAL_TRUE_MIN, MANT_DIG, MAX_EXP
//
// Every result is compared with the host libm's. Functions whose result is
// fully specified (roundings, remainders, sqrt, fma, ...) must match bit for
// bit; the others within a few units in the last place, since two correct
// libraries may round differently. NaNs match NaNs, and the sign of a zero
// must match.
//
// host_rename.h turns every plain function name into the libc's (sin is
// kora_sin), so the libc's functions are spelled kora_ here, and the host's
// are bound by symbol name. A macro argument is renamed as soon as it passes
// through a second macro, which is why the table macros below paste and
// stringify the name themselves instead of calling LIBC() and HOST().

#define LIBC(name) SFX(kora_##name)
#define HOST(name) T(host_##name)
#define HOST_LABEL(name) __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) #name SUFFIX)

// name, the interval random arguments are drawn from, allowed distance.
#define UNARY_FUNCTIONS(X)            \
    X(acos, -1, 1, 4)                 \
    X(asin, -1, 1, 4)                 \
    X(atan, -40, 40, 4)               \
    X(cos, -40, 40, 4)                \
    X(sin, -40, 40, 4)                \
    X(tan, -40, 40, 4)                \
    X(acosh, 1, 1000, 8)              \
    X(asinh, -1000, 1000, 8)          \
    X(atanh, -1, 1, 8)                \
    X(cosh, -40, 40, 8)               \
    X(sinh, -40, 40, 8)               \
    X(tanh, -20, 20, 8)               \
    X(exp, -40, 40, 4)                \
    X(exp2, -60, 60, 4)               \
    X(expm1, -40, 40, 4)              \
    X(log, 0, 1000, 4)                \
    X(log10, 0, 1000, 4)              \
    X(log1p, -1, 1000, 4)             \
    X(log2, 0, 1000, 4)               \
    X(cbrt, -1000, 1000, 4)           \
    X(erf, -6, 6, 8)                  \
    X(erfc, -6, 27, 8)                \
    X(tgamma, -20, 30, 16)            \
    X(logb, -1000, 1000, 0)           \
    X(fabs, -1000, 1000, 0)           \
    X(sqrt, 0, 1000, 0)               \
    X(ceil, -1000, 1000, 0)           \
    X(floor, -1000, 1000, 0)          \
    X(nearbyint, -1000, 1000, 0)      \
    X(rint, -1000, 1000, 0)           \
    X(round, -1000, 1000, 0)          \
    X(trunc, -1000, 1000, 0)

#define BINARY_FUNCTIONS(X)           \
    X(atan2, -100, 100, 4)            \
    X(hypot, -1000, 1000, 4)          \
    X(pow, 0, 40, 4)                  \
    X(fmod, -1000, 1000, 0)           \
    X(remainder, -1000, 1000, 0)      \
    X(copysign, -1000, 1000, 0)       \
    X(nextafter, -1000, 1000, 0)      \
    X(fdim, -1000, 1000, 0)           \
    X(fmax, -1000, 1000, 0)           \
    X(fmin, -1000, 1000, 0)

#define DECLARE1(name, lo, hi, ulps) \
    REAL T(host_##name)(REAL) __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) #name SUFFIX);
#define DECLARE2(name, lo, hi, ulps) \
    REAL T(host_##name)(REAL, REAL) __asm__(TEST_STRINGIFY(__USER_LABEL_PREFIX__) #name SUFFIX);
UNARY_FUNCTIONS(DECLARE1)
BINARY_FUNCTIONS(DECLARE2)
REAL HOST(lgamma)(REAL) HOST_LABEL(lgamma);
REAL HOST(frexp)(REAL, int *) HOST_LABEL(frexp);
REAL HOST(ldexp)(REAL, int) HOST_LABEL(ldexp);
REAL HOST(scalbn)(REAL, int) HOST_LABEL(scalbn);
REAL HOST(scalbln)(REAL, long) HOST_LABEL(scalbln);
REAL HOST(modf)(REAL, REAL *) HOST_LABEL(modf);
int HOST(ilogb)(REAL) HOST_LABEL(ilogb);
long HOST(lrint)(REAL) HOST_LABEL(lrint);
long long HOST(llrint)(REAL) HOST_LABEL(llrint);
long HOST(lround)(REAL) HOST_LABEL(lround);
long long HOST(llround)(REAL) HOST_LABEL(llround);
REAL HOST(remquo)(REAL, REAL, int *) HOST_LABEL(remquo);
REAL HOST(fma)(REAL, REAL, REAL) HOST_LABEL(fma);

static UINT T(bits)(REAL x) {
    UINT b;
    memcpy(&b, &x, sizeof b);
    return b;
}

static REAL T(from_bits)(UINT b) {
    REAL x;
    memcpy(&x, &b, sizeof x);
    return x;
}

// How many representable values apart a and b are; a huge number if only one
// is a NaN or if two zeros differ in sign.
static unsigned long long T(distance)(REAL a, REAL b) {
    if (a != a || b != b) {
        return (a != a && b != b) ? 0 : ~0ull;
    }
    UINT ua = T(bits)(a), ub = T(bits)(b);
    const UINT sign = (UINT)1 << (sizeof(UINT) * 8 - 1);
    if (a == 0 && b == 0) {
        return ua == ub ? 0 : ~0ull;
    }
    // Sign and magnitude to a number line.
    SINT ia = (ua & sign) ? -(SINT)(ua & ~sign) : (SINT)ua;
    SINT ib = (ub & sign) ? -(SINT)(ub & ~sign) : (SINT)ub;
    return ia > ib ? (unsigned long long)ia - (unsigned long long)ib
                   : (unsigned long long)ib - (unsigned long long)ia;
}

static unsigned T(mismatches);

static void T(expect)(const char *name, REAL x, REAL y, REAL got, REAL want,
                      unsigned long long ulps) {
    unsigned long long d = T(distance)(got, want);
    test_checks++;
    if (d > ulps) {
        test_failures++;
        if (++T(mismatches) <= 25) {
            test_printf("FAIL %s%s(%a, %a): libc %a, host %a (%llu apart, %llu allowed)\n", name,
                        SUFFIX, (double)x, (double)y, (double)got, (double)want, d, ulps);
        }
    }
#ifdef MATH_TEST_REPORT
    math_report(name, SUFFIX, d);
#endif
}

static const REAL T(special)[] = {
    0, 1, 2, 3, 10, 100, (REAL)0.5, (REAL)1.5, (REAL)2.5, (REAL)0.1, (REAL)1e-5, (REAL)1e-10,
    (REAL)1e-20, (REAL)0.99999, (REAL)1.00001, (REAL)3.14159265358979323846,
    (REAL)1.57079632679489661923, (REAL)0.78539816339744830962, (REAL)6.28318530717958647692,
    (REAL)2.71828182845904523536, 20, 50, 88, 89, 128, 150, 700, 709, 710, 745, 1000, 1024, 1075,
    (REAL)1e6, (REAL)1e15, (REAL)1e22, (REAL)1e30, REAL_MAX, REAL_MAX / 2, REAL_MIN, REAL_MIN * 2,
    REAL_MIN / 2, REAL_TRUE_MIN, REAL_TRUE_MIN * 3, (REAL)(1ull << 31), (REAL)(1ull << 52),
    (REAL)(1ull << 53), (REAL)(1ull << 62), __builtin_inf(),
};
#define T_SPECIALS (sizeof T(special) / sizeof T(special)[0])

// Signed special values, then NaN.
static REAL T(special_at)(size_t i) {
    if (i == 2 * T_SPECIALS) {
        return __builtin_nan("");
    }
    return i % 2 ? -T(special)[i / 2] : T(special)[i / 2];
}
#define T_SPECIAL_COUNT (2 * T_SPECIALS + 1)

// Any bit pattern that is not a NaN: every magnitude equally likely.
static REAL T(random_any)(void) {
    for (;;) {
        REAL x = T(from_bits)((UINT)next_random());
        if (x == x) {
            return x;
        }
    }
}

static REAL T(random_in)(double lo, double hi) {
    double unit = (double)(next_random() >> 11) * 0x1p-53;
    return (REAL)(lo + (hi - lo) * unit);
}

static void T(test_unary)(void) {
    static const struct {
        const char *name;
        REAL (*libc)(REAL);
        REAL (*host)(REAL);
        double lo, hi;
        unsigned ulps;
    } table[] = {
#define ROW1(name, lo, hi, ulps) {#name, SFX(kora_##name), T(host_##name), lo, hi, ulps},
        UNARY_FUNCTIONS(ROW1)
    };
    for (size_t f = 0; f < sizeof table / sizeof table[0]; f++) {
        for (size_t i = 0; i < T_SPECIAL_COUNT; i++) {
            REAL x = T(special_at)(i);
            T(expect)(table[f].name, x, 0, table[f].libc(x), table[f].host(x), table[f].ulps);
        }
        for (int i = 0; i < 3000; i++) {
            REAL x = T(random_any)();
            T(expect)(table[f].name, x, 0, table[f].libc(x), table[f].host(x), table[f].ulps);
        }
        for (int i = 0; i < 12000; i++) {
            REAL x = T(random_in)(table[f].lo, table[f].hi);
            T(expect)(table[f].name, x, 0, table[f].libc(x), table[f].host(x), table[f].ulps);
        }
    }
}

// lgamma crosses zero at 1, at 2 and between the negative integers; next to
// a zero the last places of a tiny result are noise in any implementation,
// so a small absolute difference passes too. For negative arguments the host
// is only a rough reference: next to a negative integer Apple's lgamma is off
// by thousands of units where musl's is within one (checked with mpmath;
// test_error_and_gamma has such values), so there only gross errors fail.
static void T(test_lgamma)(void) {
    const REAL epsilon = (REAL)(sizeof(REAL) == 8 ? 0x1p-52 : 0x1p-23);
    for (int i = 0; i < 20000; i++) {
        REAL x = i < 3000 ? T(random_any)() : T(random_in)(-30, 200);
        if (i < (int)T_SPECIAL_COUNT) {
            x = T(special_at)((size_t)i);
        }
        REAL got = LIBC(lgamma)(x), want = HOST(lgamma)(x);
        REAL diff = got > want ? got - want : want - got;
        REAL size = want < 0 ? -want : want;
        REAL allowed = 64 * epsilon;
        if (x < 0 && size * 0x1p20f * epsilon > allowed) {
            allowed = size * 0x1p20f * epsilon;
        }
        if (diff == diff && diff <= allowed) {
            test_checks++;
            continue;
        }
        T(expect)("lgamma", x, 0, got, want, 16);
    }
}

static void T(test_binary)(void) {
    static const struct {
        const char *name;
        REAL (*libc)(REAL, REAL);
        REAL (*host)(REAL, REAL);
        double lo, hi;
        unsigned ulps;
    } table[] = {
#define ROW2(name, lo, hi, ulps) {#name, SFX(kora_##name), T(host_##name), lo, hi, ulps},
        BINARY_FUNCTIONS(ROW2)
    };
    for (size_t f = 0; f < sizeof table / sizeof table[0]; f++) {
        int is_max_min = table[f].libc == LIBC(fmax) || table[f].libc == LIBC(fmin);
        for (size_t i = 0; i < T_SPECIAL_COUNT; i++) {
            for (size_t j = 0; j < T_SPECIAL_COUNT; j++) {
                REAL x = T(special_at)(i), y = T(special_at)(j);
                if (is_max_min && x == 0 && y == 0) {
                    continue;  // the larger of +0 and -0 is either one
                }
                T(expect)(table[f].name, x, y, table[f].libc(x, y), table[f].host(x, y),
                          table[f].ulps);
            }
        }
        for (int i = 0; i < 12000; i++) {
            REAL x, y;
            if (i < 3000) {
                x = T(random_any)();
                y = T(random_any)();
            } else {
                x = T(random_in)(table[f].lo, table[f].hi);
                y = T(random_in)(-table[f].hi, table[f].hi);
                if (table[f].libc == LIBC(nextafter) && i % 3 == 0) {
                    y = x;  // equal arguments
                }
            }
            T(expect)(table[f].name, x, y, table[f].libc(x, y), table[f].host(x, y),
                      table[f].ulps);
        }
    }
    // pow of a negative base is defined for whole exponents only.
    for (int i = 0; i < 4000; i++) {
        REAL x = T(random_in)(-20, 0);
        REAL y = (REAL)((long)(next_random() % 41) - 20);
        if (i % 4 == 0) {
            y += (REAL)0.5;
        }
        T(expect)("pow", x, y, LIBC(pow)(x, y), HOST(pow)(x, y), 4);
    }
}

// The functions with another shape: results must match exactly.
static void T(test_others)(void) {
    for (int i = 0; i < 20000; i++) {
        REAL x = i % 3 ? T(random_in)(-100000, 100000) : T(random_any)();
        if (i < (int)T_SPECIAL_COUNT) {
            x = T(special_at)((size_t)i);
        }
        int finite = x - x == 0;

        int e1 = 12345, e2 = 12345;
        REAL m1 = LIBC(frexp)(x, &e1), m2 = HOST(frexp)(x, &e2);
        T(expect)("frexp", x, 0, m1, m2, 0);
        if (finite) {
            CHECK(e1 == e2, "frexp%s(%a) exponent %d, host %d", SUFFIX, (double)x, e1, e2);
        }

        REAL w1 = 0, w2 = 0;
        REAL f1 = LIBC(modf)(x, &w1), f2 = HOST(modf)(x, &w2);
        T(expect)("modf", x, 0, f1, f2, 0);
        T(expect)("modf whole part", x, 0, w1, w2, 0);

        int n = (int)(next_random() % (2 * (MAX_EXP + MANT_DIG) + 200)) - (MAX_EXP + MANT_DIG) - 100;
        T(expect)("ldexp", x, (REAL)n, LIBC(ldexp)(x, n), HOST(ldexp)(x, n), 0);
        T(expect)("scalbn", x, (REAL)n, LIBC(scalbn)(x, n), HOST(scalbn)(x, n), 0);
        T(expect)("scalbln", x, (REAL)n, LIBC(scalbln)(x, n), HOST(scalbln)(x, n), 0);

        if (finite && x != 0) {
            CHECK(LIBC(ilogb)(x) == HOST(ilogb)(x), "ilogb%s(%a) = %d, host %d", SUFFIX, (double)x,
                  LIBC(ilogb)(x), HOST(ilogb)(x));
        }
        // Conversions to integers are only specified where the result fits.
        if (finite && x > (REAL)-0x1p62 && x < (REAL)0x1p62) {
            CHECK(LIBC(lrint)(x) == HOST(lrint)(x), "lrint%s(%a) = %ld, host %ld", SUFFIX,
                  (double)x, LIBC(lrint)(x), HOST(lrint)(x));
            CHECK(LIBC(llrint)(x) == HOST(llrint)(x), "llrint%s(%a)", SUFFIX, (double)x);
            CHECK(LIBC(lround)(x) == HOST(lround)(x), "lround%s(%a) = %ld, host %ld", SUFFIX,
                  (double)x, LIBC(lround)(x), HOST(lround)(x));
            CHECK(LIBC(llround)(x) == HOST(llround)(x), "llround%s(%a)", SUFFIX, (double)x);
        }

        REAL y = i % 2 ? T(random_in)(-1000, 1000) : T(random_any)();
        int q1 = 0, q2 = 0;
        REAL r1 = LIBC(remquo)(x, y, &q1), r2 = HOST(remquo)(x, y, &q2);
        T(expect)("remquo", x, y, r1, r2, 0);
        if (r1 == r1) {
            // Only the sign and the low three bits of the quotient are specified.
            // (some hosts return just those, so a quotient of 8 comes back as 0).
            unsigned low1 = (q1 < 0 ? 0u - (unsigned)q1 : (unsigned)q1) & 7;
            unsigned low2 = (q2 < 0 ? 0u - (unsigned)q2 : (unsigned)q2) & 7;
            CHECK(low1 == low2 && (low2 == 0 || (q1 < 0) == (q2 < 0)),
                  "remquo%s(%a, %a) quotient %d, host %d", SUFFIX, (double)x, (double)y, q1, q2);
        }

        REAL z = i % 2 ? T(random_in)(-1000, 1000) : -x * y;  // or a near cancellation
        T(expect)("fma", x, y, LIBC(fma)(x, y, z), HOST(fma)(x, y, z), 0);
    }
    // Halfway cases, where the rounding functions differ from one another.
    for (int i = -2000; i <= 2000; i++) {
        REAL x = (REAL)i / 2;
        T(expect)("round", x, 0, LIBC(round)(x), HOST(round)(x), 0);
        T(expect)("rint", x, 0, LIBC(rint)(x), HOST(rint)(x), 0);
        CHECK(LIBC(lround)(x) == HOST(lround)(x) && LIBC(lrint)(x) == HOST(lrint)(x) &&
                  LIBC(llround)(x) == HOST(llround)(x) && LIBC(llrint)(x) == HOST(llrint)(x),
              "integer rounding of %a", (double)x);
    }
}

#undef LIBC
#undef HOST
#undef HOST_LABEL
#undef UNARY_FUNCTIONS
#undef BINARY_FUNCTIONS
#undef DECLARE1
#undef DECLARE2
#undef ROW1
#undef ROW2
#undef T_SPECIALS
#undef T_SPECIAL_COUNT
