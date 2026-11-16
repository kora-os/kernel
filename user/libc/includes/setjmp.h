#ifndef LIBC_SETJMP_H
#define LIBC_SETJMP_H

#include <_libc/cdefs.h>

// What setjmp saves on AArch64: the callee-saved registers x19 to x30, the
// stack pointer and d8 to d15 (21 words), plus one spare.
typedef unsigned long jmp_buf[22];

// setjmp is a function here, not a macro (C11 7.13 allows either).
int setjmp(jmp_buf env) __attribute__((__returns_twice__));
_LIBC_NORETURN void longjmp(jmp_buf env, int val);

#endif // LIBC_SETJMP_H
