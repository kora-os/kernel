// SPDX-License-Identifier: GPL-3.0-or-later
//
// User-facing terminal: screen output, keyboard (+ UART fallback) input. See
// tty.h.

#include "tty.h"

#include "arch/irq.h"
#include "mini_uart.h"
#include "video/console_fb.h"

// Keyboard input queue. Single producer (the USB keyboard IRQ path) and single
// consumer (tty_getc) on one core, so volatile indices plus compiler barriers
// are enough: the producer publishes a byte before advancing head, and the
// consumer reads it before advancing tail.
#define INPUT_QUEUE_SIZE 256  // power of two

static char input_queue[INPUT_QUEUE_SIZE];
static volatile unsigned input_head;  // written only by the producer
static volatile unsigned input_tail;  // written only by the consumer

void tty_input_push(char c) {
    unsigned head = input_head;
    if (head - input_tail == INPUT_QUEUE_SIZE) {
        return;  // full: drop
    }
    input_queue[head % INPUT_QUEUE_SIZE] = c;
    asm volatile("" ::: "memory");
    input_head = head + 1;
}

static int input_pop(char *c) {
    unsigned tail = input_tail;
    if (tail == input_head) {
        return 0;
    }
    *c = input_queue[tail % INPUT_QUEUE_SIZE];
    asm volatile("" ::: "memory");
    input_tail = tail + 1;
    return 1;
}

char tty_getc(void) {
    // Syscalls run with PSTATE.I set (exception entry masks IRQs), so without
    // this window the keyboard, SOF and systick interrupts would all stall for
    // as long as the shell waits for input. Only this idle wait is opened up:
    // IRQ handlers (the Circle USB stack) allocate from the frame allocator,
    // which the rest of the syscall path also uses without locking.
    irq_enable();
    char c;
    for (;;) {
        if (input_pop(&c)) {
            break;
        }
        if (uart_rx_ready()) {
            c = (char)uart_getc();
            break;
        }
    }
    irq_disable();
    return c;
}

void tty_putc(char c) {
#ifndef QEMU_TESTING
    if (screen_framebuffer() != NULL) {
        screen_putc(c);
        return;
    }
#else
    screen_putc(c);
#endif
    if (c == '\n') {
        uart_putc('\r');
    }
    uart_putc((unsigned char)c);
}
