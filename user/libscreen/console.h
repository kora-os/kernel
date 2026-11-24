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
 * history, and so does the mouse's wheel. It draws in the built-in 8x8 font
 * or a PSF2 font a program gives it (console_set_font).
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
 * foreground program there, or sends it the break. In cooked mode it also
 * drops the line being typed and ends the oldest waiting read with an empty
 * line. console_run() does all of that for one program.
 */
#ifndef LIBSCREEN_CONSOLE_H
#define LIBSCREEN_CONSOLE_H

#include "libconsole/edit.h"
#include "libconsole/term.h"
#include "libconsole/term_render.h"
#include "libscreen/screen.h"

#define CONSOLE_HISTORY_LINES 500  /* scrollback */
#define CONSOLE_READERS 16         /* reads waiting at once */
#define CONSOLE_WHEEL_LINES 3      /* lines a turn of the wheel scrolls */

typedef struct console {
    screen_t *screen;
    term_t term;
    term_render_t render;
    term_font_t font;     /* the renderer's */
    void *font_data;      /* a PSF2 font's file, which it reads its glyphs from */
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

/* Whether its screen is gone: the screen manager has ended. */
int console_gone(const console_t *c);

/* The signals to wait for: its port, its screen's keys (and the manager's
 * end) and its readers' ends. */
unsigned console_signals(const console_t *c);

/* Do what the signals in `got` (sig_wait's answer) ask: take the screen's
 * keys, serve the packets, answer the reads that can be answered, drop those
 * of programs that ended, and show what changed. */
void console_service(console_t *c, unsigned got);

/* Draw the console in a PSF2 font (`size` bytes at `psf`, copied), or with
 * NULL in the built-in 8x8 font, resizing it to the cells the font gives:
 * the text stays, cut or padded (term_resize in libconsole/term.h). Its
 * programs ask for it with a CON_FONT packet (console_font in libk). 0, or
 * -1 with the console as it was: not a PSF2 font, larger than the screen, or
 * too little memory. */
int console_set_font(console_t *c, const void *psf, size_t size);

/* Show text of the client's own on the console ('\n' starts a new line). */
void console_write(console_t *c, const char *text, size_t length);

/* Call `fn(ctx)` on Ctrl+C. */
void console_on_break(console_t *c, void (*fn)(void *ctx), void *ctx);

/* What console_run() does on Ctrl+C: kill the program, or send it the break
 * signal (SIG_BREAK in <libk/koraos.h>), for a program that ends its own
 * foreground program then (the shell). */
#define CONSOLE_BREAK_KILL 0
#define CONSOLE_BREAK_SIGNAL 1

/* Run the program `name` with its stdin, stdout and stderr on the console,
 * and serve the console until it ends. 0 with its exit code in *code, or -1
 * if it could not start. Sets the break callback (`on_break` says what it
 * does) for the time it runs. If the screen is gone (the manager ended), the
 * program is killed: nothing would show its output or reach it. */
int console_run(console_t *c, const char *name, int argc, char *const argv[], int on_break,
                int *code);

/* Answer the reads still waiting (with an error), and free the console. The
 * screen stays the caller's. */
void console_close(console_t *c);

#endif
