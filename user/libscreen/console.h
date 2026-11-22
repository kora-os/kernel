/* A console on a screen: a CON: handler, as on AmigaOS
 * (project_doc/console-plan.md).
 *
 * The console is a terminal of the console library (libconsole/term.h)
 * drawn on a screen of the screen manager (libscreen/screen.h), with the
 * line editor (libconsole/edit.h) between the screen's keys and the programs.
 * Programs reach it through its message port, as their stdin, stdout or
 * stderr (spawn_std with STD_PORT handles): the libc sends it packets
 * (<_libc/console.h>). Writes are shown at once; reads wait for a line in
 * cooked mode (the default) or for any key in raw mode (console_mode() in
 * <libk/koraos.h>). Shift+PageUp and Shift+PageDown scroll back through its
 * history.
 *
 *   screen_t *s = screen_open("my console");
 *   console_t *c = console_open(s);
 *   struct std_handle std[3] = {{STD_PORT, console_port(c)}, ...};
 *   int pid = spawn_std(name, argc, argv, SPAWN_NOWAIT | SPAWN_SIGNAL(bit), std);
 *   for (;;) {
 *       unsigned got = sig_wait(console_signals(c) | SIG_MASK(bit));
 *       console_service(c, got);
 *       ...
 *   }
 *
 * Ctrl+C calls the break callback (console_on_break): a client kills its
 * foreground program there.
 */
#ifndef LIBSCREEN_CONSOLE_H
#define LIBSCREEN_CONSOLE_H

#include "libconsole/edit.h"
#include "libconsole/term.h"
#include "libconsole/term_render.h"
#include "libscreen/screen.h"

#define CONSOLE_HISTORY_LINES 500  /* scrollback */
#define CONSOLE_READERS 16         /* reads waiting at once */

typedef struct console {
    screen_t *screen;
    term_t term;
    term_render_t render;
    uint32_t cursor_col;  /* where the renderer last drew the cursor */
    con_edit_t edit;
    int port;
    int port_bit;     /* a packet on the port */
    int end_bit;      /* a program waiting for input ended */
    struct {
        struct con_packet *packet;
        int sender;
    } waiting[CONSOLE_READERS];  /* reads waiting for input, oldest first */
    int waiting_count;
    void (*on_break)(void *ctx);
    void *break_ctx;
} console_t;

/* A console filling the screen `s`. NULL when out of memory or ports. */
console_t *console_open(screen_t *s);

/* Its port, for the STD_PORT handles of its programs. */
int console_port(const console_t *c);

/* The signals to wait for: its port, its screen's keys and its readers'
 * ends. */
unsigned console_signals(const console_t *c);

/* Do what the signals in `got` (sig_wait's answer) ask: take the screen's
 * keys, serve the packets, answer the reads that can be answered, drop those
 * of programs that ended, and show what changed. */
void console_service(console_t *c, unsigned got);

/* Show text of the client's own on the console ('\n' starts a new line). */
void console_write(console_t *c, const char *text, size_t length);

/* Call `fn(ctx)` on Ctrl+C. */
void console_on_break(console_t *c, void (*fn)(void *ctx), void *ctx);

/* Answer the reads still waiting (with an error), and free the console. The
 * screen stays the caller's. */
void console_close(console_t *c);

#endif
