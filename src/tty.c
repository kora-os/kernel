// SPDX-License-Identifier: GPL-3.0-or-later
//
// User-facing terminal: screen output, keyboard (+ UART fallback) input. See
// tty.h.

#include "tty.h"

#include "arch/irq.h"
#include "console.h"
#include "mini_uart.h"
#include "video/console_fb.h"

// Input queue. Every producer (the USB keyboard IRQ path, and serial input
// routed here) runs with IRQs masked on one core, so pushes never interleave;
// the single consumer is tty_getc. Volatile indices plus compiler barriers are
// enough: a producer publishes a byte before advancing head, and the consumer
// reads it before advancing tail.
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

// Serial input routing. The UART belongs either to the kernel debug console
// (console.c) or, as a fallback keyboard, to this terminal; Ctrl-T switches.
#define SERIAL_SWITCH_KEY 0x14  // Ctrl-T

#ifdef QEMU_TESTING
static bool serial_to_tty = true;   // no USB keyboard: the UART drives the shell
#else
static bool serial_to_tty = false;  // the UART is the kernel debug console
#endif

static void announce_serial_route(void) {
    if (serial_to_tty) {
        uart_puts("\n[serial -> screen terminal; Ctrl-T for the kernel console]\n");
    } else {
        uart_puts("\n[serial -> kernel console; Ctrl-T for the screen terminal]\n");
        console_prompt();
    }
}

void tty_poll_serial(void) {
    // Masked so the UART interrupt and the idle-loop poll never both drain.
    uint64_t daif;
    asm volatile("mrs %0, daif" : "=r"(daif));
    asm volatile("msr daifset, #2" ::: "memory");
    while (uart_rx_ready()) {
        char c = (char)uart_getc();
        if (c == SERIAL_SWITCH_KEY) {
            serial_to_tty = !serial_to_tty;
            announce_serial_route();
        } else if (serial_to_tty) {
            tty_input_push(c);
        } else {
            console_input(c);
        }
    }
    asm volatile("msr daif, %0" ::"r"(daif) : "memory");
}

static void serial_isr(void *ctx) {
    (void)ctx;
    tty_poll_serial();
}

void tty_serial_init(void) {
    announce_serial_route();
    uart_rx_irq_enable(serial_isr);
}

// Scrollback requests from the keyboard (IRQ context), in half screens. They
// are applied from the input wait loop below, so the screen is never drawn
// from an interrupt handler.
static volatile int scroll_request;

void tty_scrollback(int halfpages) {
    scroll_request += halfpages;
}

static void apply_scroll_request(void) {
    if (scroll_request == 0) {
        return;
    }
    irq_disable();
    int request = scroll_request;
    scroll_request = 0;
    if (request != 0) {
        screen_scroll_view(request);
        screen_flush();
    }
    irq_enable();
}

void tty_flush(void) {
    screen_flush();
}

char tty_getc(void) {
    tty_flush();  // whatever was echoed or printed must be visible while we wait

    // Syscalls run with PSTATE.I set (exception entry masks IRQs), so without
    // this window the keyboard, SOF and systick interrupts would all stall for
    // as long as the shell waits for input. Only this idle wait is opened up:
    // IRQ handlers (the Circle USB stack) allocate from the frame allocator,
    // which the rest of the syscall path also uses without locking.
    irq_enable();
    char c;
    for (;;) {
        // Also polled here, not only from the UART interrupt: on boards where
        // IRQs are not wired up yet (Pi 4, until its GIC is), this is the only
        // way serial input arrives.
        tty_poll_serial();
        apply_scroll_request();
        if (input_pop(&c)) {
            break;
        }
    }
    irq_disable();
    return c;
}

// Screen output with the Unix newline translation (ONLCR): the terminal treats
// LF as a pure line feed, as xterm does, so '\n' is sent as CR LF.
static void screen_out(char c) {
    if (c == '\n') {
        screen_putc('\r');
    }
    screen_putc(c);
}

void tty_putc(char c) {
#ifndef QEMU_TESTING
    if (screen_framebuffer() != NULL) {
        screen_out(c);
        return;
    }
#else
    screen_out(c);
#endif
    if (c == '\n') {
        uart_putc('\r');
    }
    uart_putc((unsigned char)c);
}
