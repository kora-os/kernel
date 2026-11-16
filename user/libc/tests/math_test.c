// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for <math.h> (src/math: musl's libm, the long double wrappers
// and nexttoward).
//
// 1. By hand: the special values C11 Annex F fixes (signed zeros, infinities,
//    NaNs, domain errors), results that must be exact, the classification and
//    comparison macros, and the long double functions.
// 2. Differential (math_test_impl.h, for double and for float): every
//    function over special values and many random arguments against the
//    host's libm; exactly for the fully specified functions, within a few
//    units in the last place for the others.
//
// On hosts that are not arm64, the fifteen functions AArch64 does in one
// instruction (sqrt, floor, fma, ...) come from the host itself
// (tests/math_host_arch.c), so their rows compare the host with the host;
// arm64 hosts and libcprobe on the target run the real ones.
//
// Build with -DMATH_TEST_REPORT to print, per function, the largest distance
// from the host's result that was seen.

#include "test.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint64_t rng = 0x9e3779b97f4a7c15ull;

static uint64_t next_random(void) {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

#ifdef MATH_TEST_REPORT
static struct {
    const char *name, *suffix;
    unsigned long long worst;
} report[128];
static int reported;

static void math_report(const char *name, const char *suffix, unsigned long long d) {
    int i = 0;
    while (i < reported && (report[i].name != name || report[i].suffix != suffix)) {
        i++;
    }
    if (i == reported) {
        report[reported].name = name;
        report[reported].suffix = suffix;
        report[reported++].worst = 0;
    }
    if (d != ~0ull && d > report[i].worst) {
        report[i].worst = d;
    }
}

static void print_report(void) {
    for (int i = 0; i < reported; i++) {
        test_printf("%-16s%-2s worst distance %llu\n", report[i].name, report[i].suffix,
                    report[i].worst);
    }
}
#endif

#define REAL double
#define UINT uint64_t
#define SINT int64_t
#define SFX(name) name
#define SUFFIX ""
#define T(name) name##_d
#define REAL_MAX DBL_MAX
#define REAL_MIN DBL_MIN
#define REAL_TRUE_MIN DBL_TRUE_MIN
#define MANT_DIG DBL_MANT_DIG
#define MAX_EXP DBL_MAX_EXP
#include "math_test_impl.h"
#undef REAL
#undef UINT
#undef SINT
#undef SFX
#undef SUFFIX
#undef T
#undef REAL_MAX
#undef REAL_MIN
#undef REAL_TRUE_MIN
#undef MANT_DIG
#undef MAX_EXP

#define REAL float
#define UINT uint32_t
#define SINT int32_t
#define SFX(name) name##f
#define SUFFIX "f"
#define T(name) name##_f
#define REAL_MAX FLT_MAX
#define REAL_MIN FLT_MIN
#define REAL_TRUE_MIN FLT_TRUE_MIN
#define MANT_DIG FLT_MANT_DIG
#define MAX_EXP FLT_MAX_EXP
#include "math_test_impl.h"

// Same bits, or both NaN.
static int same(double a, double b) {
    return (a != a && b != b) || bits_d(a) == bits_d(b);
}

static int samef(float a, float b) {
    return (a != a && b != b) || bits_f(a) == bits_f(b);
}

#define PI 0x1.921fb54442d18p+1
static const double inf = HUGE_VAL;
static volatile double zero = 0.0;  // volatile: nothing is folded at compile time

static void test_header(void) {
    CHECK(sizeof(float_t) == sizeof(float) && sizeof(double_t) == sizeof(double),
          "float_t and double_t are float and double");
    CHECK(HUGE_VAL == inf && HUGE_VALF > FLT_MAX && HUGE_VALL > LDBL_MAX, "HUGE_VAL");
    CHECK(sizeof(INFINITY) == sizeof(float) && sizeof(NAN) == sizeof(float),
          "INFINITY and NAN are floats");
    double not_a_number = NAN;
    CHECK(not_a_number != not_a_number && INFINITY > FLT_MAX, "NAN, INFINITY");
    CHECK(math_errhandling == MATH_ERREXCEPT && MATH_ERRNO == 1, "math_errhandling");
    CHECK(FP_NAN != FP_INFINITE && FP_INFINITE != FP_ZERO && FP_ZERO != FP_SUBNORMAL &&
              FP_SUBNORMAL != FP_NORMAL && FP_NAN != FP_NORMAL,
          "the classes are distinct");
}

static void test_classification(void) {
    double nan_d = zero / zero;
    CHECK(fpclassify(1.5) == FP_NORMAL && fpclassify(zero) == FP_ZERO &&
              fpclassify(-zero) == FP_ZERO && fpclassify(DBL_TRUE_MIN) == FP_SUBNORMAL &&
              fpclassify(DBL_MIN / 2) == FP_SUBNORMAL && fpclassify(DBL_MIN) == FP_NORMAL &&
              fpclassify(inf) == FP_INFINITE && fpclassify(-inf) == FP_INFINITE &&
              fpclassify(nan_d) == FP_NAN,
          "fpclassify(double)");
    float f0 = 0.0f, fmin = FLT_TRUE_MIN, fnan = NAN, finf = INFINITY;
    CHECK(fpclassify(1.5f) == FP_NORMAL && fpclassify(f0) == FP_ZERO &&
              fpclassify(fmin) == FP_SUBNORMAL && fpclassify(finf) == FP_INFINITE &&
              fpclassify(fnan) == FP_NAN,
          "fpclassify(float)");
    long double l1 = 1.5L, l0 = 0.0L, linf = HUGE_VALL, lnan = nanl("");
    CHECK(fpclassify(l1) == FP_NORMAL && fpclassify(l0) == FP_ZERO &&
              fpclassify(linf) == FP_INFINITE && fpclassify(lnan) == FP_NAN,
          "fpclassify(long double)");
    // The function behind the macros where long double is binary128 (the
    // target; libcprobe checks its bit tests there).
    int __fpclassifyl(long double x);
    CHECK(__fpclassifyl(l1) == FP_NORMAL && __fpclassifyl(l0) == FP_ZERO &&
              __fpclassifyl(-linf) == FP_INFINITE && __fpclassifyl(lnan) == FP_NAN &&
              __fpclassifyl(LDBL_TRUE_MIN) == FP_SUBNORMAL && __fpclassifyl(LDBL_MAX) == FP_NORMAL,
          "__fpclassifyl");
    CHECK(isfinite(1.0) && isfinite(DBL_MAX) && !isfinite(inf) && !isfinite(nan_d) &&
              isfinite(f0) && !isfinite(finf) && isfinite(l1) && !isfinite(lnan),
          "isfinite");
    CHECK(isinf(inf) && isinf(-inf) && !isinf(DBL_MAX) && !isinf(nan_d) && isinf(finf) &&
              isinf(linf) && !isinf(l1),
          "isinf");
    CHECK(isnan(nan_d) && !isnan(inf) && !isnan(1.0) && isnan(fnan) && isnan(lnan) && !isnan(l1),
          "isnan");
    CHECK(isnormal(1.0) && isnormal(DBL_MIN) && !isnormal(DBL_MIN / 2) && !isnormal(zero) &&
              !isnormal(inf) && !isnormal(nan_d) && isnormal(1.0f) && !isnormal(fmin) &&
              isnormal(l1) && !isnormal(l0),
          "isnormal");
    CHECK(signbit(-1.0) && !signbit(1.0) && signbit(-zero) && !signbit(zero) && signbit(-inf) &&
              signbit(-1.0f) && !signbit(f0) && signbit(-l1) && !signbit(l1),
          "signbit");
    CHECK(isgreater(2.0, 1.0) && !isgreater(1.0, 2.0) && !isgreater(nan_d, 1.0) &&
              isgreaterequal(2.0, 2.0) && !isgreaterequal(1.0, nan_d) && isless(1.0, 2.0) &&
              !isless(nan_d, nan_d) && islessequal(2.0, 2.0) && !islessequal(nan_d, 2.0) &&
              islessgreater(1.0, 2.0) && !islessgreater(2.0, 2.0) && !islessgreater(nan_d, 2.0),
          "the comparison macros are false for a NaN");
    CHECK(isunordered(nan_d, 1.0) && isunordered(1.0, nan_d) && !isunordered(1.0, 2.0) &&
              isunordered(fnan, 1.0f),
          "isunordered");
}

static void test_trigonometric(void) {
    CHECK(same(sin(zero), 0.0) && same(sin(-zero), -0.0) && same(tan(-zero), -0.0) &&
              same(asin(-zero), -0.0) && same(atan(-zero), -0.0),
          "odd functions keep the sign of zero");
    CHECK(cos(zero) == 1.0 && cos(-zero) == 1.0 && same(acos(1.0), 0.0), "cos(0), acos(1)");
    CHECK(same(sin(inf), NAN) && same(cos(-inf), NAN) && same(tan(inf), NAN),
          "sin, cos, tan of infinity are NaN");
    CHECK(same(asin(1.5), NAN) && same(acos(-1.5), NAN), "asin, acos outside [-1, 1]");
    CHECK(atan(inf) == PI / 2 && atan(-inf) == -PI / 2 && asin(1.0) == PI / 2 && acos(-1.0) == PI,
          "atan(inf), asin(1), acos(-1)");
    CHECK(atan2(zero, -zero) == PI && atan2(-zero, -zero) == -PI && same(atan2(zero, zero), 0.0) &&
              same(atan2(-zero, zero), -0.0),
          "atan2 of zeros");
    CHECK(atan2(1.0, zero) == PI / 2 && atan2(-1.0, -zero) == -PI / 2 && atan2(zero, -1.0) == PI &&
              same(atan2(-zero, 1.0), -0.0),
          "atan2 on the axes");
    CHECK(atan2(inf, inf) == PI / 4 && atan2(-inf, inf) == -PI / 4 && atan2(inf, -inf) == 3 * PI / 4 &&
              atan2(1.0, -inf) == PI && same(atan2(-1.0, inf), -0.0) && atan2(inf, 1.0) == PI / 2,
          "atan2 of infinities");
    // Huge arguments need the exact reduction by pi (__rem_pio2_large).
    // Correctly rounded values (mpmath); the library is within one place.
    CHECK(distance_d(sin(1e22), -0x1.b453ab76bf397p-1) <= 1 &&
              distance_d(cos(1e22), 0x1.0be2cef01c8f4p-1) <= 1,
          "sin and cos of 1e22: %a %a", sin(1e22), cos(1e22));
    CHECK(sinf(0.0f) == 0.0f && cosf(0.0f) == 1.0f && samef(tanf(-0.0f), -0.0f),
          "float trigonometric functions at zero");
    CHECK(samef(sinf(INFINITY), NAN), "sinf(inf): %a", (double)sinf(INFINITY));
    // musl gives the float just below pi/2, so that the result never exceeds it.
    CHECK(distance_f(atanf(INFINITY), 0x1.921fb6p+0f) <= 1, "atanf(inf): %a",
          (double)atanf(INFINITY));
}

static void test_hyperbolic(void) {
    CHECK(same(sinh(-zero), -0.0) && cosh(zero) == 1.0 && same(tanh(-zero), -0.0) &&
              same(asinh(-zero), -0.0) && same(atanh(-zero), -0.0) && same(acosh(1.0), 0.0),
          "hyperbolic functions at zero");
    CHECK(sinh(inf) == inf && sinh(-inf) == -inf && cosh(-inf) == inf && tanh(inf) == 1.0 &&
              tanh(-inf) == -1.0 && asinh(-inf) == -inf && acosh(inf) == inf,
          "hyperbolic functions at infinity");
    CHECK(atanh(1.0) == inf && atanh(-1.0) == -inf && same(atanh(2.0), NAN) &&
              same(acosh(0.5), NAN) && same(acosh(-inf), NAN),
          "atanh at its poles, domain errors");
    CHECK(sinh(1000.0) == inf && cosh(1000.0) == inf && sinh(-1000.0) == -inf,
          "sinh and cosh overflow to infinity");
    CHECK(tanhf(INFINITY) == 1.0f && coshf(0.0f) == 1.0f && atanhf(1.0f) == INFINITY &&
              samef(acoshf(0.0f), NAN),
          "float hyperbolic special values");
}

static void test_exponential(void) {
    CHECK(exp(zero) == 1.0 && exp(-zero) == 1.0 && exp(inf) == inf && same(exp(-inf), 0.0) &&
              exp(710.0) == inf && same(exp(-746.0), 0.0),
          "exp special values and limits");
    CHECK(exp2(10.0) == 1024.0 && exp2(-1074.0) == DBL_TRUE_MIN && exp2(1023.0) == 0x1p1023 &&
              exp2(1024.0) == inf && same(exp2(-1075.0), 0.0) && exp2(0.5) == 0x1.6a09e667f3bcdp+0,
          "exp2 is exact for whole exponents");
    CHECK(same(expm1(-zero), -0.0) && expm1(-inf) == -1.0 && expm1(inf) == inf, "expm1");
    CHECK(same(log(1.0), 0.0) && log(zero) == -inf && log(-zero) == -inf && log(inf) == inf &&
              same(log(-1.0), NAN) && same(log(-inf), NAN),
          "log special values");
    CHECK(log2(8.0) == 3.0 && log2(0x1p-1074) == -1074.0 && log2(0x1p1023) == 1023.0 &&
              log10(1000.0) == 3.0 && log10(1e-5) == -5.0 && log10(zero) == -inf,
          "log2 and log10 of exact powers");
    CHECK(same(log1p(-zero), -0.0) && log1p(-1.0) == -inf && same(log1p(-2.0), NAN) &&
              log1p(inf) == inf,
          "log1p");
    CHECK(expf(0.0f) == 1.0f && exp2f(-149.0f) == FLT_TRUE_MIN && log2f(0.125f) == -3.0f &&
              logf(0.0f) == -INFINITY && samef(logf(-1.0f), NAN) && expf(89.0f) == INFINITY,
          "float exponential special values");

    int e = 99;
    CHECK(frexp(8.0, &e) == 0.5 && e == 4, "frexp(8)");
    CHECK(frexp(-0.75, &e) == -0.75 && e == 0, "frexp(-0.75)");
    CHECK(frexp(DBL_TRUE_MIN, &e) == 0.5 && e == -1073, "frexp of the smallest subnormal");
    e = 99;
    CHECK(same(frexp(-zero, &e), -0.0) && e == 0, "frexp(-0)");
    CHECK(frexp(inf, &e) == inf && same(frexp(NAN, &e), NAN), "frexp of infinity and NaN");
    CHECK(ldexp(1.0, 10) == 1024.0 && ldexp(1.0, -1074) == DBL_TRUE_MIN && ldexp(1.0, 1024) == inf &&
              same(ldexp(1.0, -1075), 0.0) && ldexp(0.75, 2) == 3.0 && ldexp(inf, -5) == inf,
          "ldexp");
    CHECK(scalbn(3.0, -1) == 1.5 && scalbn(DBL_TRUE_MIN, 2097) == 0x1p1023 &&
              scalbln(1.0, 100000L) == inf && same(scalbln(1.0, -100000L), 0.0) &&
              scalbn(1.0, INT_MAX) == inf && same(scalbn(-1.0, INT_MIN), -0.0),
          "scalbn and scalbln, far out of range too");
    CHECK(ilogb(1.0) == 0 && ilogb(1024.5) == 10 && ilogb(0.3) == -2 && ilogb(DBL_TRUE_MIN) == -1074 &&
              ilogb(zero) == FP_ILOGB0 && ilogb(NAN) == FP_ILOGBNAN && ilogb(inf) == INT_MAX,
          "ilogb");
    CHECK(logb(8.5) == 3.0 && logb(DBL_TRUE_MIN) == -1074.0 && logb(zero) == -inf &&
              logb(-inf) == inf,
          "logb");
    double whole = 0;
    CHECK(modf(-3.75, &whole) == -0.75 && whole == -3.0, "modf(-3.75)");
    CHECK(same(modf(-inf, &whole), -0.0) && whole == -inf, "modf(-inf)");
    CHECK(same(modf(-2.0, &whole), -0.0) && whole == -2.0, "modf of a whole number");
}

static void test_power(void) {
    CHECK(pow(2.0, 10.0) == 1024.0 && pow(10.0, 2.0) == 100.0 && pow(3.0, 4.0) == 81.0 &&
              pow(-2.0, 3.0) == -8.0 && pow(-2.0, 4.0) == 16.0 && pow(2.0, -1074.0) == DBL_TRUE_MIN &&
              pow(2.0, 1024.0) == inf && pow(4.0, 0.5) == 2.0,
          "pow: exact powers");
    CHECK(pow(NAN, zero) == 1.0 && pow(inf, -zero) == 1.0 && pow(1.0, NAN) == 1.0 &&
              pow(-1.0, inf) == 1.0 && pow(-1.0, -inf) == 1.0,
          "pow: the cases that are 1 whatever the other argument");
    CHECK(pow(zero, -3.0) == inf && pow(-zero, -3.0) == -inf && pow(-zero, -2.0) == inf &&
              same(pow(-zero, 3.0), -0.0) && same(pow(-zero, 2.0), 0.0),
          "pow of zeros");
    CHECK(same(pow(-8.0, 1.0 / 3), NAN) && same(pow(NAN, 1.0), NAN), "pow domain error");
    CHECK(pow(0.5, inf) == 0.0 && pow(2.0, inf) == inf && pow(0.5, -inf) == inf &&
              same(pow(-inf, -3.0), -0.0) && pow(-inf, 3.0) == -inf && pow(-inf, 2.0) == inf,
          "pow of infinities");
    CHECK(sqrt(4.0) == 2.0 && sqrt(2.0) == 0x1.6a09e667f3bcdp+0 && same(sqrt(-zero), -0.0) &&
              sqrt(inf) == inf && same(sqrt(-1.0), NAN) && sqrt(0x1p-1074) == 0x1p-537,
          "sqrt");
    CHECK(cbrt(27.0) == 3.0 && cbrt(-8.0) == -2.0 && same(cbrt(-zero), -0.0) && cbrt(-inf) == -inf,
          "cbrt");
    CHECK(hypot(3.0, 4.0) == 5.0 && hypot(-3.0, -4.0) == 5.0 && hypot(inf, NAN) == inf &&
              hypot(NAN, -inf) == inf && hypot(1e300, 1e300) == 0x1.6a09e667f3bcdp+0 * 1e300 &&
              hypot(zero, -zero) == 0.0,
          "hypot, without overflow in between");
    CHECK(fabs(-zero) == 0.0 && !signbit(fabs(-zero)) && fabs(-inf) == inf && fabs(-2.5) == 2.5,
          "fabs");
    CHECK(powf(2.0f, 10.0f) == 1024.0f && powf(NAN, 0.0f) == 1.0f && sqrtf(9.0f) == 3.0f &&
              cbrtf(-27.0f) == -3.0f && hypotf(3.0f, 4.0f) == 5.0f && fabsf(-1.5f) == 1.5f &&
              powf(0.0f, -1.0f) == INFINITY,
          "float power functions");
}

static void test_error_and_gamma(void) {
    CHECK(same(erf(-zero), -0.0) && erf(inf) == 1.0 && erf(-inf) == -1.0 && erfc(inf) == 0.0 &&
              erfc(-inf) == 2.0 && erfc(zero) == 1.0 && erf(10.0) == 1.0,
          "erf and erfc");
    CHECK(same(lgamma(1.0), 0.0) && same(lgamma(2.0), 0.0) && lgamma(zero) == inf &&
              lgamma(-1.0) == inf && lgamma(inf) == inf && lgamma(-inf) == inf,
          "lgamma");
    CHECK(tgamma(1.0) == 1.0 && tgamma(5.0) == 24.0 && tgamma(11.0) == 3628800.0,
          "tgamma is exact for small whole numbers");
    CHECK(distance_d(tgamma(0.5), 0x1.c5bf891b4ef6bp+0) <= 2, "tgamma(0.5) is sqrt(pi): %a",
          tgamma(0.5));
    // lgamma against correctly rounded values (mpmath), negative arguments
    // next to the poles included: there the host libms are not all reliable
    // (see test_lgamma in math_test_impl.h).
    static const double lgamma_values[][2] = {
        {-0x1.0000000000000p-1, 0x1.43f89a3f0edd6p+0},  {-0x1.8000000000000p+0, 0x1.b858151820f86p-1},
        {-0x1.4000000000000p+1, -0x1.ccbf9f5ed0f16p-5}, {-0x1.fffeb074a771dp-1, 0x1.7069eb880bd07p+3},
        {-0x1.4fffe4f67151dp+4, -0x1.1683c1f7260fcp+5}, {-0x1.c001a36e2eb1cp+2, 0x1.5eb55db04a2afp-1},
        {-0x1.9200000000000p+6, -0x1.6ce6a5dbefb91p+8}, {0x1.0624dd2f1a9fcp-10, 0x1.ba0f3807161acp+2},
        {0x1.8000000000000p+0, -0x1.eeb95b094c191p-4},  {0x1.d99999999999ap+1, 0x1.6d9625e359b87p+0},
        {0x1.9100000000000p+6, 0x1.6848d8e681f58p+8},   {0x1.2a05f20000000p+33, 0x1.9a43710f467c1p+37},
        {-0x1.bfe7a7cc01120p+2, -0x1.01551e34ce03fp+1},
    };
    for (size_t i = 0; i < sizeof lgamma_values / sizeof lgamma_values[0]; i++) {
        double got = lgamma(lgamma_values[i][0]), want = lgamma_values[i][1];
        // Within four places, or 2^-50 in absolute terms where the result is
        // small because lgamma is about to cross zero (at -2.5, for one).
        CHECK(distance_d(got, want) <= 4 || fabs(got - want) <= 0x1p-50,
              "lgamma(%a) = %a, want %a", lgamma_values[i][0], got, want);
    }
    CHECK(tgamma(zero) == inf && tgamma(-zero) == -inf && same(tgamma(-1.0), NAN) &&
              same(tgamma(-inf), NAN) && tgamma(inf) == inf && tgamma(172.0) == inf &&
              tgamma(171.0) < inf,
          "tgamma poles and overflow");
    CHECK(erff(INFINITY) == 1.0f && tgammaf(4.0f) == 6.0f && samef(lgammaf(1.0f), 0.0f),
          "float error and gamma functions");
}

static void test_nearest_integer(void) {
    CHECK(floor(-0.5) == -1.0 && floor(2.9) == 2.0 && same(floor(-zero), -0.0) &&
              same(ceil(-0.5), -0.0) && ceil(2.1) == 3.0 && same(trunc(-0.9), -0.0) &&
              trunc(2.9) == 2.0 && floor(inf) == inf && same(ceil(NAN), NAN),
          "floor, ceil, trunc");
    CHECK(round(0.5) == 1.0 && round(-0.5) == -1.0 && round(2.5) == 3.0 && round(-2.5) == -3.0 &&
              same(round(-0.4), -0.0) && round(0.49999999999999994) == 0.0 &&
              round(0x1p52 + 1) == 0x1p52 + 1,
          "round goes away from zero at a half");
    CHECK(rint(0.5) == 0.0 && rint(1.5) == 2.0 && rint(2.5) == 2.0 && rint(-2.5) == -2.0 &&
              same(rint(-0.5), -0.0) && nearbyint(3.5) == 4.0 && nearbyint(4.5) == 4.0,
          "rint and nearbyint go to the even neighbour");
    CHECK(lround(2.5) == 3 && lround(-2.5) == -3 && llround(-0.5) == -1 && lround(0.49999999999999994) == 0 &&
              lrint(2.5) == 2 && lrint(3.5) == 4 && llrint(-2.5) == -2 && lrint(-0.5) == 0,
          "lround and lrint");
    CHECK(llround(0x1p62) == 1ll << 62 && llrint(-0x1p62) == -(1ll << 62) &&
              lround(123456789012345.0) == 123456789012345l,
          "large values convert exactly");
    CHECK(floorf(-0.5f) == -1.0f && ceilf(2.1f) == 3.0f && roundf(2.5f) == 3.0f &&
              rintf(2.5f) == 2.0f && truncf(-2.9f) == -2.0f && lroundf(-2.5f) == -3 &&
              lrintf(3.5f) == 4 && llrintf(2.5f) == 2 && llroundf(2.5f) == 3 &&
              nearbyintf(-0.5f) == 0.0f,
          "float nearest integer functions");
}

static void test_remainder(void) {
    CHECK(fmod(5.5, 2.0) == 1.5 && fmod(-5.5, 2.0) == -1.5 && fmod(5.5, -2.0) == 1.5 &&
              same(fmod(-4.0, 2.0), -0.0) && fmod(3.0, inf) == 3.0 && same(fmod(inf, 1.0), NAN) &&
              same(fmod(1.0, zero), NAN) && fmod(DBL_MAX, DBL_TRUE_MIN) == 0.0,
          "fmod");
    CHECK(remainder(5.0, 2.0) == 1.0 && remainder(7.0, 2.0) == -1.0 && remainder(-7.0, 2.0) == 1.0 &&
              remainder(5.5, inf) == 5.5 && same(remainder(1.0, zero), NAN),
          "remainder rounds the quotient to even");
    int q = 0;
    CHECK(remquo(7.0, 2.0, &q) == -1.0 && q == 4, "remquo(7, 2): quotient %d", q);
    CHECK(remquo(-10.0, 3.0, &q) == -1.0 && q == -3, "remquo(-10, 3): quotient %d", q);
    CHECK(fmodf(5.5f, 2.0f) == 1.5f && remainderf(7.0f, 2.0f) == -1.0f &&
              remquof(7.0f, 2.0f, &q) == -1.0f && q == 4,
          "float remainder functions");
}

static void test_manipulation(void) {
    CHECK(copysign(1.0, -zero) == -1.0 && copysign(-3.0, 2.0) == 3.0 && signbit(copysign(NAN, -1.0)) &&
              same(copysign(zero, -1.0), -0.0),
          "copysign");
    CHECK(same(nan(""), NAN) && same(nan("123"), NAN) && samef(nanf(""), NAN), "nan");
    CHECK(nextafter(1.0, 2.0) == 1.0 + DBL_EPSILON && nextafter(1.0, 0.0) == 1.0 - DBL_EPSILON / 2 &&
              nextafter(zero, 1.0) == DBL_TRUE_MIN && nextafter(zero, -1.0) == -DBL_TRUE_MIN &&
              nextafter(DBL_MAX, inf) == inf && nextafter(inf, 0.0) == DBL_MAX &&
              same(nextafter(zero, -zero), -0.0) && nextafter(-DBL_TRUE_MIN, 1.0) == 0.0 &&
              same(nextafter(NAN, 1.0), NAN),
          "nextafter");
    CHECK(nexttoward(1.0, 2.0L) == 1.0 + DBL_EPSILON && nexttoward(1.0, 0.0L) == 1.0 - DBL_EPSILON / 2 &&
              nexttoward(1.0, 1.0L) == 1.0 && nexttoward(zero, 1.0L) == DBL_TRUE_MIN &&
              nexttoward(zero, -1.0L) == -DBL_TRUE_MIN && nexttoward(DBL_MAX, HUGE_VALL) == inf &&
              nexttoward(-1.0, -2.0L) == -1.0 - DBL_EPSILON && nexttoward(-1.0, 0.0L) == -1.0 + DBL_EPSILON / 2,
          "nexttoward");
    CHECK(same(nexttoward(zero, -0.0L), -0.0) && same(nexttoward(NAN, 1.0L), NAN) &&
              same(nexttoward(1.0, nanl("")), NAN) && nexttoward(inf, 0.0L) == DBL_MAX,
          "nexttoward: equal zeros, NaNs, infinity");
    CHECK(nexttowardf(1.0f, 2.0L) == 1.0f + FLT_EPSILON && nexttowardf(1.0f, 1.0L) == 1.0f &&
              nexttowardf(0.0f, -1.0L) == -FLT_TRUE_MIN && nexttowardf(FLT_MAX, HUGE_VALL) == INFINITY &&
              nexttowardf(1.0f, 0.0L) == 1.0f - FLT_EPSILON / 2 && samef(nexttowardf(NAN, 0.0L), NAN),
          "nexttowardf");
    // The direction comes from the long double, not from its nearest double:
    // 1 + 2^-40 is not a float, and where long double is wider than double
    // neither is 1 + LDBL_EPSILON a double.
    CHECK(nexttowardf(1.0f, 1.0L + 0x1p-40L) == 1.0f + FLT_EPSILON,
          "nexttowardf towards a value between floats");
    CHECK(nexttoward(1.0, 1.0L + LDBL_EPSILON) == 1.0 + DBL_EPSILON,
          "nexttoward towards a value between doubles");
    CHECK(fdim(3.0, 1.0) == 2.0 && same(fdim(1.0, 3.0), 0.0) && fdim(inf, 1.0) == inf &&
              same(fdim(1.0, NAN), NAN) && same(fdim(-inf, inf), 0.0),
          "fdim");
    CHECK(fmax(1.0, 2.0) == 2.0 && fmax(NAN, 1.0) == 1.0 && fmax(1.0, NAN) == 1.0 &&
              fmin(1.0, 2.0) == 1.0 && fmin(NAN, 1.0) == 1.0 && fmin(-inf, 1.0) == -inf &&
              same(fmax(NAN, NAN), NAN),
          "fmax and fmin ignore one NaN");
    CHECK(fma(2.0, 3.0, 4.0) == 10.0 && fma(DBL_MAX, 2.0, -DBL_MAX) == DBL_MAX &&
              fma(1.0 + DBL_EPSILON, 1.0 - DBL_EPSILON, -1.0) == -DBL_EPSILON * DBL_EPSILON &&
              same(fma(inf, zero, 1.0), NAN),
          "fma rounds once");
    CHECK(copysignf(1.0f, -0.0f) == -1.0f && nextafterf(1.0f, 2.0f) == 1.0f + FLT_EPSILON &&
              fdimf(3.0f, 1.0f) == 2.0f && fmaxf(NAN, 1.0f) == 1.0f && fminf(1.0f, 2.0f) == 1.0f &&
              fmaf(2.0f, 3.0f, 4.0f) == 10.0f,
          "float manipulation functions");
}

// The long double functions compute in double: each must give exactly what
// the double function gives.
static void test_long_double(void) {
    static const double values[] = {0.0, -0.0, 0.5, -0.5, 1.0, 2.5, -3.75, 10.0, 100.5,
                                    1e-10, 1e10, 0.1, -0.7};
#define LD1(name)                                                                  \
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {               \
        double x = values[i];                                                     \
        CHECK(same((double)name##l((long double)x), name(x)), #name "l(%a)", x);  \
    }
    LD1(acos) LD1(asin) LD1(atan) LD1(cos) LD1(sin) LD1(tan) LD1(acosh) LD1(asinh) LD1(atanh)
    LD1(cosh) LD1(sinh) LD1(tanh) LD1(exp) LD1(exp2) LD1(expm1) LD1(log) LD1(log10) LD1(log1p)
    LD1(log2) LD1(logb) LD1(cbrt) LD1(fabs) LD1(sqrt) LD1(erf) LD1(erfc) LD1(lgamma) LD1(tgamma)
    LD1(ceil) LD1(floor) LD1(nearbyint) LD1(rint) LD1(round) LD1(trunc)
#define LD2(name)                                                                      \
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {                   \
        for (size_t j = 0; j < sizeof values / sizeof values[0]; j++) {               \
            double x = values[i], y = values[j];                                      \
            CHECK(same((double)name##l((long double)x, (long double)y), name(x, y)),  \
                  #name "l(%a, %a)", x, y);                                           \
        }                                                                             \
    }
    LD2(atan2) LD2(hypot) LD2(pow) LD2(fmod) LD2(remainder) LD2(copysign) LD2(nextafter)
    LD2(fdim) LD2(fmax) LD2(fmin)
    int e = 0, q = 0;
    long double whole = 0;
    CHECK(frexpl(8.0L, &e) == 0.5L && e == 4, "frexpl");
    CHECK(ldexpl(0.75L, 2) == 3.0L && scalbnl(1.0L, 10) == 1024.0L && scalblnl(1.0L, -1L) == 0.5L,
          "ldexpl, scalbnl, scalblnl");
    CHECK(ilogbl(1024.5L) == 10, "ilogbl");
    CHECK(modfl(-3.75L, &whole) == -0.75L && whole == -3.0L, "modfl");
    CHECK(lrintl(2.5L) == 2 && llrintl(3.5L) == 4 && lroundl(2.5L) == 3 && llroundl(-2.5L) == -3,
          "integer conversions of long double");
    CHECK(remquol(7.0L, 2.0L, &q) == -1.0L && q == 4, "remquol");
    long double not_a_number = nanl("");
    CHECK(not_a_number != not_a_number, "nanl");
    CHECK(nexttowardl(1.0L, 2.0L) == (long double)(1.0 + DBL_EPSILON), "nexttowardl");
    CHECK(fmal(2.0L, 3.0L, 4.0L) == 10.0L, "fmal");
    CHECK(fabsl(-2.5L) == 2.5L && copysignl(2.5L, -1.0L) == -2.5L && sqrtl(16.0L) == 4.0L,
          "fabsl, copysignl, sqrtl");
}

static void test_differential(void) {
    test_unary_d();
    test_lgamma_d();
    test_binary_d();
    test_others_d();
    test_unary_f();
    test_lgamma_f();
    test_binary_f();
    test_others_f();
#ifdef MATH_TEST_REPORT
    print_report();
#endif
}

TEST_MAIN(test_header, test_classification, test_trigonometric, test_hyperbolic,
          test_exponential, test_power, test_error_and_gamma, test_nearest_integer,
          test_remainder, test_manipulation, test_long_double, test_differential)
