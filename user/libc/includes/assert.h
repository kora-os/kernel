// SPDX-License-Identifier: GPL-3.0-or-later
//
// <assert.h> (C11 7.2). Deliberately without an include guard: the standard
// has assert() follow the NDEBUG in force at each inclusion, so including the
// header again after changing NDEBUG switches it.
//
// A failed assertion writes "Assertion failed: EXPRESSION, function NAME,
// file FILE, line N" to stderr, as one write, then calls abort().

#include <_libc/cdefs.h>

#undef assert

#ifdef NDEBUG
#define assert(ignore) ((void)0)
#else
_LIBC_NORETURN void __assert_fail(const char *expression, const char *file, int line,
                                  const char *function);
#define assert(expression) \
    ((expression) ? (void)0 : __assert_fail(#expression, __FILE__, __LINE__, __func__))
#endif

// C11's spelling of _Static_assert; C23 makes static_assert a keyword.
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && __STDC_VERSION__ < 202311L && \
    !defined(static_assert)
#define static_assert _Static_assert
#endif
