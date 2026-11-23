// SPDX-License-Identifier: GPL-3.0-or-later
//
// User-facing terminal: screen output and keyboard input. See tty.h.

#include "tty.h"

#include "arch/irq.h"
#include "console.h"
#include "input/input.h"
#include "input/input_tty.h"
#include "input/keylog.h"
#include "lib/panic.h"
#include "arch/spinlock.h"
#include "mini_uart.h"
#include "proc/task.h"
#include "video/console_fb.h"

// Input queue of bytes, drained by tasks reading the console. Two things fill
// it: tty_input_push(), for input that is bytes already (the UART and the
// terminal's status replies), and the keyboards' events, which readers move
// here from the input event queue (drain_key_events). input_lock (IRQ-safe)
// covers the queue, scroll_request and the wait queue's condition.
#define INPUT_QUEUE_SIZE 256  // power of two

static struct spinlock input_lock = SPINLOCK_INIT("tty input");
static char input_queue[INPUT_QUEUE_SIZE];
static unsigned input_head;
static unsigned input_tail;

// Readers sleep here until input (or a scrollback request) arrives.
static struct wait_queue input_waiters;

void tty_input_push(char c) {
    uint64_t flags = spin_lock_irqsave(&input_lock);
    unsigned head = input_head;
    if (head - input_tail != INPUT_QUEUE_SIZE) {  // full: drop
        input_queue[head % INPUT_QUEUE_SIZE] = c;
        input_head = head + 1;
        wait_queue_wake_all(&input_waiters);
    }
    spin_unlock_irqrestore(&input_lock, flags);
}

// Scrollback requests, in half screens, from the keyboard's Shift+PageUp and
// Shift+PageDown. The task waiting for input applies them (see tty_getc).
static int scroll_request;

// Keyboards post key events to the input event queue (input/input.h); this
// is its consumer for now. Move the pending events into the byte queue as
// the bytes a terminal sends for them (input/input_tty.h), in task context,
// stopping while a whole sequence may not fit: events then wait in their own
// queue, and no escape sequence is ever cut short. Called with input_lock
// held.
static void drain_key_events(void) {
    struct input_event event;
    while (INPUT_QUEUE_SIZE - (input_head - input_tail) >= INPUT_TTY_MAX_BYTES &&
           input_poll(&event)) {
        keylog_note(&event, KEYLOG_TO_CONSOLE, task_current()->pid, 0);
        struct input_tty_bytes out;
        input_tty_translate(&event, &out);
        for (unsigned i = 0; i < out.length; i++) {
            input_queue[input_head % INPUT_QUEUE_SIZE] = out.bytes[i];
            input_head++;
        }
        scroll_request += out.scroll;
    }
}

// A key event was queued (from IRQ context): wake the readers, which drain it.
// Taking input_lock orders this against a reader about to sleep, which has
// either not checked the event queue yet or is already on the wait queue.
void input_consumer_wake(void) {
    uint64_t flags = spin_lock_irqsave(&input_lock);
    wait_queue_wake_all(&input_waiters);
    spin_unlock_irqrestore(&input_lock, flags);
}

// Called with input_lock held.
static int input_pop(char *c) {
    unsigned tail = input_tail;
    if (tail == input_head) {
        return 0;
    }
    *c = input_queue[tail % INPUT_QUEUE_SIZE];
    input_tail = tail + 1;
    return 1;
}

// The serial line is the kernel debug console's (console.c). serial_lock
// (IRQ-safe) keeps the UART interrupt and the boot thread's polling loop from
// draining the UART at once. The debug console's commands run under it (lock
// order: serial, then the others).
static struct spinlock serial_lock = SPINLOCK_INIT("serial");

void tty_poll_serial(void) {
    uint64_t flags = spin_lock_irqsave(&serial_lock);
    while (uart_rx_ready()) {
        console_input((char)uart_getc());
    }
    spin_unlock_irqrestore(&serial_lock, flags);
}

static void serial_isr(void *ctx) {
    (void)ctx;
    tty_poll_serial();
}

void tty_serial_init(void) {
    console_prompt();
    uart_rx_irq_enable(serial_isr);
}

// Called with input_lock held; returns and clears the pending request.
static int take_scroll_request(void) {
    int request = scroll_request;
    scroll_request = 0;
    return request;
}

void tty_flush(void) {
    screen_flush();
}

int tty_getc(void) {
    tty_flush();  // whatever was echoed or printed must be visible while we wait

    // Sleep until a UART interrupt pushes a byte or a keyboard (USB, VirtIO)
    // posts a key event. The queues are checked and the sleep entered under
    // input_lock, so input arriving in between still wakes us; other tasks
    // run meanwhile, and with nothing runnable the scheduler idles with IRQs
    // open.
    // Scrollback is drawn outside the lock: the screen belongs to tasks (the
    // BKL), not to the interrupt handlers input_lock fends off.
    uint64_t flags = spin_lock_irqsave(&input_lock);
    char c;
    for (;;) {
        drain_key_events();
        int scroll = take_scroll_request();
        if (scroll != 0) {
            spin_unlock_irqrestore(&input_lock, flags);
            screen_scroll_view(scroll);
            screen_flush();
            flags = spin_lock_irqsave(&input_lock);
            continue;
        }
        if (input_pop(&c)) {
            break;
        }
        if (task_killed()) {
            spin_unlock_irqrestore(&input_lock, flags);
            return -1;  // its process ended: give up so the task can die
        }
        wait_queue_sleep(&input_waiters, &input_lock);
    }
    spin_unlock_irqrestore(&input_lock, flags);
    return (unsigned char)c;
}

long tty_read(char *buf, size_t len) {
    int ch = tty_getc();
    if (ch < 0) {
        keylog_flush();
        return -1;  // killed: the task dies on its way back to EL0
    }
    buf[0] = (char)ch;
    size_t n = 1;
    uint64_t flags = spin_lock_irqsave(&input_lock);
    drain_key_events();
    while (n < len && input_pop(&buf[n])) {
        n++;
    }
    spin_unlock_irqrestore(&input_lock, flags);
    keylog_flush();  // the events taken, now that no lock is held
    return (long)n;
}

// Screen output with the Unix newline translation (ONLCR): the terminal treats
// LF as a pure line feed, as xterm does, so '\n' is sent as CR LF.
static void screen_out(char c) {
    if (c == '\n') {
        screen_putc('\r');
    }
    screen_putc(c);
}

void tty_serial_copy(const char *buf, size_t len) {
    unsigned long flags = serial_out_begin();
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            uart_putc('\r');
        }
        uart_putc((unsigned char)buf[i]);
    }
    serial_out_end(flags);
}

void tty_write(const char *buf, size_t len) {
    bool screen = screen_framebuffer() != NULL;
    if (screen) {
        for (size_t i = 0; i < len; i++) {
            screen_out(buf[i]);
        }
    }
    // With no screen the serial line is the terminal; with conlog on, it
    // carries a copy (console.c).
    if (!screen || console_conlog()) {
        tty_serial_copy(buf, len);
    }
}

void tty_putc(char c) {
    tty_write(&c, 1);
}
