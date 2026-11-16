/* KoraOS: stands in for musl's internal <features.h> (src/include/features.h),
 * which the copied sources include. Found on the include path of the math
 * sources only; it is not a public header. */
#ifndef LIBC_MATH_FEATURES_H
#define LIBC_MATH_FEATURES_H

/* Internal symbols stay out of a program's dynamic symbol table. */
#define hidden __attribute__((__visibility__("hidden")))

/* musl gives some functions a second, non-ISO name (drem, lgamma_r,
 * signgam, ...). KoraOS exports the ISO names only. */
#define weak_alias(old, new) _Static_assert(1, "no alias")

#endif
