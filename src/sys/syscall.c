#include "sys/syscall.h"
#include "arch/trapframe.h"
#include "common.h"
#include "mini_uart.h"
#include "proc/task.h"

// write(fd, buf, len): only fd==1 (stdout) is supported for now. With the flat
// identity map the user buffer is directly readable from EL1.
static long sys_write(int fd, const char *buf, uint64_t len) {
    if (fd != 1) {
        return -1;
    }
    for (uint64_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') {
            uart_putc('\r');
        }
        uart_putc((unsigned char)c);
    }
    return (long)len;
}

void syscall_handle(struct trapframe *tf) {
    uint64_t num = tf->regs[8];
    uint64_t a0 = tf->regs[0];
    uint64_t a1 = tf->regs[1];
    uint64_t a2 = tf->regs[2];
    long ret = -1;

    switch (num) {
    case SYS_write:
        ret = sys_write((int)a0, (const char *)a1, a2);
        break;
    case SYS_exit:
        task_exit((int)a0);  // does not return
        break;
    default:
        ret = -1;
        break;
    }

    tf->regs[0] = (uint64_t)ret;
}
