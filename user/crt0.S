// KoraOS user program entry stub. The kernel enters here at EL0 with SP_EL0
// already pointing at the top of the user stack. We call main(), then pass its
// return value to exit(); exit() does not return.
//
// _start is the default lld entry symbol and is what the ELF header's e_entry
// points at.

.section .text
.global _start
_start:
    bl   main          // int main(void); result in w0
    bl   exit          // exit(w0) -- no return
1:  b    1b            // safety net if exit ever returns
