// First embedded KoraOS user program. Freestanding AArch64, runs in EL0,
// uses the KoraOS syscall ABI (x8 = number, x0..x2 = args, svc #0).
//
// It lives in the kernel's .text section and runs in place at its
// identity-mapped address (the code region is EL0 read+execute).

.section .text
.global user_hello_start
.global user_hello_end

user_hello_start:
    // write(1, msg, msg_len)
    mov x0, #1
    adr x1, msg
    mov x2, #(msg_end - msg)
    mov x8, #0                  // SYS_write
    svc #0

    // exit(0)
    mov x0, #0
    mov x8, #1                  // SYS_exit
    svc #0

    // exit() should not return; spin just in case.
1:  b 1b

msg:
    .ascii "Hello from userland\n"
msg_end:
user_hello_end:
