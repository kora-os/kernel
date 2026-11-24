// SPDX-License-Identifier: GPL-3.0-or-later
//
// MicroPython's HAL on KoraOS (py/mphal.h): the standard streams, the console's
// modes, ticks and delays, the break, and how much the GC heap may grow.
//
// The break (Ctrl+C, SIG_BREAK) is the program's to handle (main.c allocates
// it). While Python runs, the VM looks for one at most every 10 ms
// (kora_vm_hook, from MICROPY_VM_HOOK_POLL) and raises KeyboardInterrupt. A
// break left unhandled for longer, in a long C call, is still set when a
// second one comes, and the kernel ends the program then.
//
// A read from a console handler goes as a packet of our own (as the libc's
// would, _libc/console.h), so the program waits for its answer and for the
// break at once: Ctrl+C ends the wait with CHAR_CTRL_C, which the line editor
// takes as a new line and input() as KeyboardInterrupt. The packet stays with
// the console, and the next read waits for its answer.
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "libk/koraos.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/runtime.h"
#include "shared/readline/readline.h"

#define BREAK_POLL_US 10000

static bool break_due(void) {
    return (sig_set(0, SIG_MASK(SIG_BREAK)) & SIG_MASK(SIG_BREAK)) != 0;
}

void kora_vm_hook(void) {
    static clock_t next;
    clock_t now = clock();
    if (now < next) {
        return;
    }
    next = now + BREAK_POLL_US;
    if (break_due()) {
        mp_sched_keyboard_interrupt();
    }
}

// Input: stdin's console handler (its port), or a file or the kernel console
// through the libc.
static struct {
    bool ready;      // looked at stdin's handle
    int port;        // its console handler's, 0 for none
    int reply;       // our reply port, which sets reply_bit
    int reply_bit;
    bool pending;    // `packet` is with the console
    struct con_packet packet;
    char buf[64];
    size_t pos, len;  // what the last answer brought, not taken yet
} in;

static void input_init(void) {
    in.ready = true;
    struct std_handle handles[3];
    if (std_handles(handles) != 0 || handles[0].kind != STD_PORT) {
        return;
    }
    int bit = sig_alloc(-1);
    int reply = bit >= 0 ? port_create(NULL) : -1;
    if (reply <= 0 || port_signal(reply, bit) != 0) {
        return;  // read through the libc, without Ctrl+C at the prompt
    }
    in.port = handles[0].value;
    in.reply = reply;
    in.reply_bit = bit;
}

// Our packet's answer, if it came: 1 and the bytes in `in.buf`, 0 for the end
// of input or a handler gone, -1 when it has not come yet.
static int take_answer(void) {
    struct port_message got;
    while (port_get(in.reply, &got) == 1) {
        if (got.msg == &in.packet) {
            in.pending = false;
        }
    }
    if (in.pending) {
        return -1;
    }
    if (in.packet.result <= 0) {
        return 0;
    }
    in.pos = 0;
    in.len = (size_t)in.packet.result;
    return 1;
}

int mp_hal_stdin_rx_chr(void) {
    fflush(stdout);
    if (!in.ready) {
        input_init();
    }
    if (in.pos < in.len) {
        return (unsigned char)in.buf[in.pos++];
    }
    if (in.port == 0) {
        int c = getchar();
        return c == EOF ? CHAR_CTRL_D : c;
    }
    if (!in.pending) {
        in.packet = (struct con_packet){CON_READ, -1, in.buf, sizeof in.buf};
        if (port_put(in.port, &in.packet, in.reply) != 0) {
            return CHAR_CTRL_D;  // the console is gone
        }
        in.pending = true;
    }
    // The port is looked at before each wait: an answer that came with a
    // break had its signal taken by that wait.
    for (;;) {
        int answer = take_answer();
        if (answer == 0) {
            return CHAR_CTRL_D;
        }
        if (answer == 1) {
            return (unsigned char)in.buf[in.pos++];
        }
        unsigned got = sig_wait(SIG_MASK(in.reply_bit) | SIG_MASK(SIG_BREAK));
        if (got & SIG_MASK(SIG_BREAK)) {
            return CHAR_CTRL_C;
        }
    }
}

// Output is buffered by the libc and flushed before each read, so what was
// printed is on the screen when a key is awaited.

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    return fwrite(str, 1, len, stdout);
}

// The console starts a new line on '\n' itself (ONLCR), so "cooked" output
// needs no change.
void mp_hal_stdout_tx_strn_cooked(const char *str, size_t len) {
    fwrite(str, 1, len, stdout);
}

void mp_hal_stdout_tx_str(const char *str) {
    fputs(str, stdout);
}

void mp_hal_stdio_mode_raw(void) {
    fflush(stdout);
    console_mode(CON_RAW);
}

void mp_hal_stdio_mode_orig(void) {
    fflush(stdout);
    console_mode(CON_COOKED);
}

// clock() is microseconds since boot, read from the generic timer.

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)(clock() / 1000);
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)clock();
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return (mp_uint_t)clock();
}

// Sleep in slices, so a break interrupts it with KeyboardInterrupt.
void mp_hal_delay_ms(mp_uint_t ms) {
    clock_t end = clock() + (clock_t)ms * 1000;
    for (;;) {
        if (break_due()) {
            mp_sched_keyboard_interrupt();
        }
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
        clock_t now = clock();
        if (now >= end) {
            return;
        }
        clock_t left = (end - now) / 1000;
        msleep(left > 20 ? 20 : left > 0 ? (unsigned long)left : 1);
    }
}

// A tick is 10 ms: shorter waits spin on the clock.
void mp_hal_delay_us(mp_uint_t us) {
    if (us >= 10000) {
        mp_hal_delay_ms(us / 1000);
        return;
    }
    clock_t end = clock() + (clock_t)us;
    while (clock() < end) {
    }
}

// How much a new heap area may take (MICROPY_GC_SPLIT_HEAP_AUTO): half the
// free memory, so the rest of the system keeps the other half.
size_t gc_get_max_new_split(void) {
    struct sysinfo info;
    if (sysinfo(&info) != 0) {
        return 0;
    }
    return (size_t)(info.pages_free * info.page_size / 2);
}
