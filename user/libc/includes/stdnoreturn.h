// SPDX-License-Identifier: GPL-3.0-or-later
//
// <stdnoreturn.h> (C11 7.23). After including it, write __attribute__((
// __noreturn__)) rather than __attribute__((noreturn)): the macro below
// rewrites the bare word.
#ifndef LIBC_STDNORETURN_H
#define LIBC_STDNORETURN_H

#ifndef __cplusplus
#define noreturn _Noreturn
#endif

#endif // LIBC_STDNORETURN_H
