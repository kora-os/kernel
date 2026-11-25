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
#include <string.h>
#include <time.h>

#include "libconsole/keys.h"
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
    int mode;         // the console's mode as we set it (CON_COOKED at start)
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

// Wait for input from stdin's console handler: 1 with something in
// `in.buf` from `in.pos`, 0 at the end of input (or the handler gone), -1 for
// a break. Only for a console handler (in.port).
static int input_wait(void) {
    if (in.pos < in.len) {
        return 1;
    }
    if (!in.pending) {
        in.packet = (struct con_packet){CON_READ, -1, in.buf, sizeof in.buf};
        if (port_put(in.port, &in.packet, in.reply) != 0) {
            return 0;  // the console is gone
        }
        in.pending = true;
    }
    // The port is looked at before each wait: an answer that came with a
    // break had its signal taken by that wait.
    for (;;) {
        int answer = take_answer();
        if (answer >= 0) {
            return answer;
        }
        unsigned got = sig_wait(SIG_MASK(in.reply_bit) | SIG_MASK(SIG_BREAK));
        if (got & SIG_MASK(SIG_BREAK)) {
            return -1;
        }
    }
}

static void input_ready(void) {
    fflush(stdout);
    if (!in.ready) {
        input_init();
    }
}

// In CON_EVENTS mode a key's bytes, as a terminal sends them (keys.c), for
// what reads bytes meanwhile: input() and the line editor in code KoraCLI
// runs. The console stays in that mode, so keys typed ahead stay events for
// KoraCLI's editor.
static con_key_t translated;
static unsigned translated_pos;

static int event_byte(void) {
    while (translated_pos >= translated.length) {
        struct con_key_event event;
        int got = kora_read_key(&event);
        if (got <= 0) {
            return got == 0 ? CHAR_CTRL_D : CHAR_CTRL_C;
        }
        con_key_bytes(CON_KEY_DOWN_EVENT, event.modifiers, event.keycode, event.codepoint,
                      &translated);
        translated_pos = 0;
    }
    return (unsigned char)translated.bytes[translated_pos++];
}

int mp_hal_stdin_rx_chr(void) {
    input_ready();
    if (in.port == 0) {
        int c = getchar();
        return c == EOF ? CHAR_CTRL_D : c;
    }
    if (in.mode == CON_EVENTS) {
        return event_byte();
    }
    int got = input_wait();
    return got > 0 ? (unsigned char)in.buf[in.pos++] : got == 0 ? CHAR_CTRL_D : CHAR_CTRL_C;
}

int kora_console_mode(int mode) {
    input_ready();
    if (console_mode(mode) != 0) {
        return -1;
    }
    // What came for the old mode is not for the new one. A read still with
    // the console is answered in the new mode, and read as such.
    in.mode = mode;
    in.pos = in.len = 0;
    translated.length = translated_pos = 0;
    return 0;
}

int kora_console_get_mode(void) {
    return in.mode;
}

int kora_read_key(struct con_key_event *event) {
    input_ready();
    if (in.port == 0 || in.mode != CON_EVENTS) {
        return -2;
    }
    for (;;) {
        int got = input_wait();
        if (got <= 0) {
            return got;
        }
        if (in.len - in.pos >= sizeof *event) {
            memcpy(event, in.buf + in.pos, sizeof *event);
            in.pos += sizeof *event;
            return 1;
        }
        in.pos = in.len;  // bytes from before the mode changed
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
    kora_console_mode(CON_RAW);
}

void mp_hal_stdio_mode_orig(void) {
    kora_console_mode(CON_COOKED);
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
