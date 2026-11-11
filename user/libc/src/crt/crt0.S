// SPDX-License-Identifier: GPL-3.0-or-later
//
// Program entry. The kernel enters _start at EL0 with SP_EL0 at the top of
// the user stack, x0 = argc and x1 = argv (a NULL-terminated array on the
// stack), so main(argc, argv) gets them untouched. Its return value goes to
// exit(), which runs the atexit handlers (and, later, flushes stdio) before
// ending the program; exit() does not return.
//
// _start is the entry symbol (-e _start); it lives in the libc archive and
// the linker pulls it in for the entry like any other undefined symbol.

.section .text
.global _start
.type _start, %function
_start:
    mov  x29, #0       // outermost frame: end of any frame-pointer chain
    mov  x30, #0
    bl   main          // int main(int argc, char **argv); result in w0
    bl   exit          // exit(w0): no return
1:  b    1b            // safety net if exit ever returned
.size _start, . - _start
