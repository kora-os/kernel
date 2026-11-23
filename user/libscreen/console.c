/* A console on a screen. See console.h. */
#include "libscreen/console.h"
#include "libconsole/keys.h"

static void *term_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    return calloc(1, bytes);
}

/* The terminal's answers to status requests (a cursor position report)
 * come back as typed input, as a real terminal's do. */
static void term_reply(void *ctx, const char *bytes, size_t length) {
    console_t *c = ctx;
    con_edit_input(&c->edit, bytes, length);
}

static void echo(void *ctx, const char *bytes, size_t length) {
    console_t *c = ctx;
    for (size_t i = 0; i < length; i++) {
        term_putc(&c->term, bytes[i]);
    }
}

/* A line entered, as plain text, for the serial line when conlog is on. */
static void entered(void *ctx, const char *text, size_t length) {
    (void)ctx;
    conlog(text, length);
}

/* Programs' text, with their '\n' starting a new line (ONLCR), and a copy on
 * the serial line when conlog is on. */
static void show(console_t *c, const char *text, size_t length) {
    conlog(text, length);
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            term_putc(&c->term, '\r');
        }
        term_putc(&c->term, text[i]);
    }
}

/* Draw what changed and have the manager copy those rows to the display. */
static void flush(console_t *c) {
    term_t *t = &c->term;
    int low = -1, high = -1;
    for (uint32_t row = 0; row < t->rows; row++) {
        if (t->dirty[row]) {
            low = low < 0 ? (int)row : low;
            high = (int)row;
        }
    }
    // The cursor's rows, the old and the new, if it moved, came or went.
    bool shown = term_cursor_shown(t);
    if (shown != c->render.cursor_drawn ||
        (shown && (t->row != c->render.cursor_row || t->col != c->cursor_col))) {
        int cursor_rows[2] = {c->render.cursor_drawn ? (int)c->render.cursor_row : -1,
                              shown ? (int)t->row : -1};
        for (int i = 0; i < 2; i++) {
            if (cursor_rows[i] >= 0) {
                low = low < 0 || cursor_rows[i] < low ? cursor_rows[i] : low;
                high = cursor_rows[i] > high ? cursor_rows[i] : high;
            }
        }
    }
    if (low < 0) {
        return;
    }
    term_render_flush(&c->render);
    c->cursor_col = t->col;
    screen_damage(c->screen, 0, low * TERM_CELL_HEIGHT, (int)c->screen->width,
                  (high - low + 1) * TERM_CELL_HEIGHT);
}

console_t *console_open(screen_t *s) {
    console_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->screen = s;
    term_host_t host = {term_alloc, term_reply, c};
    if (!term_init(&c->term, s->width / TERM_CELL_WIDTH, s->height / TERM_CELL_HEIGHT,
                   CONSOLE_HISTORY_LINES, &host)) {
        free(c);
        return NULL;
    }
    con_edit_init(&c->edit, echo, c);
    c->edit.entered = entered;
    c->port = port_create(NULL);
    c->port_bit = sig_alloc(-1);
    c->end_bit = sig_alloc(-1);
    if (c->port < 0 || c->port_bit < 0 || c->end_bit < 0 ||
        port_signal(c->port, c->port_bit) != 0) {
        if (c->port >= 0) {
            port_delete(c->port);
        }
        if (c->port_bit >= 0) {
            sig_free(c->port_bit);
        }
        if (c->end_bit >= 0) {
            sig_free(c->end_bit);
        }
        free(c);
        return NULL;
    }
    term_surface_t surface;
    term_surface_init(&surface, s->pixels, s->width, s->height, s->pitch);
    term_render_init(&c->render, &c->term, &surface, 0, 0);
    term_render_all(&c->render);
    screen_damage(s, 0, 0, (int)s->width, (int)s->height);
    return c;
}

int console_gone(const console_t *c) {
    return c->screen->gone;
}

int console_port(const console_t *c) {
    return c->port;
}

unsigned console_signals(const console_t *c) {
    return screen_signal(c->screen) | SIG_MASK(c->port_bit) | SIG_MASK(c->end_bit);
}

void console_on_break(console_t *c, void (*fn)(void *ctx), void *ctx) {
    c->on_break = fn;
    c->break_ctx = ctx;
}

static void answer(struct con_packet *packet, long result) {
    packet->result = result;
    port_reply(packet);
}

static void drop_waiting(console_t *c, int i) {
    for (int j = i; j < c->waiting_count - 1; j++) {
        c->waiting[j] = c->waiting[j + 1];
    }
    c->waiting_count--;
}

static void keys(console_t *c) {
    struct input_event event;
    while (screen_event(c->screen, &event)) {
        con_key_t key;
        con_key_bytes(event.type, event.modifiers, event.keycode, event.codepoint, &key);
        if (key.scroll != 0) {
            term_scroll_view(&c->term, key.scroll * (int)(c->term.rows / 2));
            continue;
        }
        if (key.length == 0) {
            continue;
        }
        if (c->term.view_offset != 0) {
            term_scroll_view(&c->term, -(int)c->term.view_offset);  /* back to the live screen */
        }
        con_edit_place(&c->edit, c->term.cols, c->term.col);
        con_edit_input(&c->edit, key.bytes, key.length);
        if (con_edit_break(&c->edit) && c->on_break != NULL) {
            c->on_break(c->break_ctx);
        }
    }
}

static void packets(console_t *c) {
    struct port_message got;
    while (port_get(c->port, &got) == 1) {
        if (got.kind != PORT_MESSAGE) {
            continue;
        }
        struct con_packet *packet = got.msg;
        switch (packet->op) {
        case CON_WRITE:
            show(c, packet->buf, packet->length);
            answer(packet, (long)packet->length);
            break;
        case CON_READ:
            if (packet->length == 0) {
                answer(packet, 0);
            } else if (c->waiting_count == CONSOLE_READERS ||
                       sig_on_end(got.sender, c->end_bit) != 0) {
                answer(packet, -1);  /* too many, or its program is gone */
            } else {
                c->waiting[c->waiting_count].packet = packet;
                c->waiting[c->waiting_count].sender = got.sender;
                c->waiting_count++;
            }
            break;
        case CON_MODE:
            con_edit_mode(&c->edit, (int)packet->length);
            answer(packet, 0);
            break;
        default:
            answer(packet, -1);
            break;
        }
    }
}

/* The reads of programs that ended: watching a program again fails once it
 * has gone. Their packets are answered so the kernel can free them. */
static void ended_readers(console_t *c) {
    for (int i = 0; i < c->waiting_count;) {
        if (sig_on_end(c->waiting[i].sender, c->end_bit) != 0) {
            answer(c->waiting[i].packet, -1);
            drop_waiting(c, i);
        } else {
            i++;
        }
    }
}

/* Answer the oldest reads while there is input for them. */
static void reads(console_t *c) {
    while (c->waiting_count > 0) {
        struct con_packet *packet = c->waiting[0].packet;
        bool end;
        size_t n = con_edit_read(&c->edit, packet->buf, packet->length, &end);
        if (n == 0 && !end) {
            return;
        }
        answer(packet, (long)n);
        int sender = c->waiting[0].sender;
        drop_waiting(c, 0);
        bool still = false;
        for (int i = 0; i < c->waiting_count; i++) {
            still |= c->waiting[i].sender == sender;
        }
        if (!still) {
            sig_on_end(sender, -1);
        }
    }
}

void console_service(console_t *c, unsigned got) {
    if (got & screen_signal(c->screen)) {
        keys(c);
        screen_gone(c->screen);  /* the manager's end sets the same signal */
    }
    packets(c);
    if (got & SIG_MASK(c->end_bit)) {
        ended_readers(c);
    }
    reads(c);
    flush(c);
}

void console_write(console_t *c, const char *text, size_t length) {
    show(c, text, length);
    flush(c);
}

struct running {
    int pid;
    int on_break;
};

static void break_program(void *ctx) {
    struct running *r = ctx;
    if (r->on_break == CONSOLE_BREAK_SIGNAL) {
        sig_send(r->pid, SIG_MASK(SIG_BREAK));
    } else {
        kill(r->pid);
    }
}

int console_run(console_t *c, const char *name, int argc, char *const argv[], int on_break,
                int *code) {
    int end_bit = sig_alloc(-1);
    if (end_bit < 0) {
        return -1;
    }
    struct std_handle std[3];
    for (int i = 0; i < 3; i++) {
        std[i] = (struct std_handle){STD_PORT, c->port};
    }
    struct running r = {spawn_std(name, argc, argv, SPAWN_NOWAIT | SPAWN_SIGNAL(end_bit), std),
                        on_break};
    if (r.pid < 0) {
        sig_free(end_bit);
        return -1;
    }
    console_on_break(c, break_program, &r);
    int killed = 0;
    for (;;) {
        unsigned got = sig_wait(console_signals(c) | SIG_MASK(end_bit));
        console_service(c, got);
        if ((got & SIG_MASK(end_bit)) && waitpid(r.pid, code, WNOHANG) == r.pid) {
            break;
        }
        if (console_gone(c) && !killed) {
            kill(r.pid);  /* nothing shows its output, and no key reaches it */
            killed = 1;
        }
    }
    console_on_break(c, NULL, NULL);
    sig_free(end_bit);
    return 0;
}

void console_close(console_t *c) {
    while (c->waiting_count > 0) {
        answer(c->waiting[0].packet, -1);
        drop_waiting(c, 0);
    }
    port_delete(c->port);
    sig_free(c->port_bit);
    sig_free(c->end_bit);
    free(c);
}
