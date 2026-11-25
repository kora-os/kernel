/* A console on a screen. See console.h. */
#include "libscreen/console.h"
#include "libconsole/keys.h"

static void *term_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    return calloc(1, bytes);
}

static void term_free(void *ctx, void *buffer) {
    (void)ctx;
    free(buffer);
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
    int cell_height = (int)c->render.font->height;
    for (int i = 0; i < 2; i++) {
        if (c->bars[i].shown && c->bars[i].term.dirty[0]) {
            term_render_flush(&c->bars[i].render);
            screen_damage(c->screen, 0, c->bars[i].render.y, (int)c->screen->width, cell_height);
        }
    }
    if (low < 0) {
        return;
    }
    term_render_flush(&c->render);
    c->cursor_col = t->col;
    screen_damage(c->screen, 0, c->render.y + low * cell_height, (int)c->screen->width,
                  (high - low + 1) * cell_height);
}

/* A bar's text, from the start of its one row, in reverse video and cut at
 * the width, the rest of the row filled. Nothing goes past the last column:
 * the text ends where a glyph there would wrap. */
static void bar_text(console_t *c, int which) {
    term_t *t = &c->bars[which].term;
    const char *text = c->bars[which].text;
    size_t length = c->bars[which].length;
    static const char start[] = "\x1b[?25l\x1b[7m\x1b[H";
    for (const char *p = start; *p; p++) {
        term_putc(t, *p);
    }
    for (uint32_t col = 0; col < t->cols; col++) {
        term_putc(t, ' ');
    }
    term_putc(t, '\r');  /* the row is full: back to its start, no wrap */
    t->wrap_pending = false;
    for (size_t i = 0; i < length && !t->wrap_pending; i++) {
        if (text[i] != '\n' && text[i] != '\r') {  /* one row: no new lines */
            term_putc(t, text[i]);
        }
    }
}

/* Lay the screen out for `font`: the bars shown, a row each at the top and
 * the bottom, and the grid between. 0, or -1 with the grid as it was when it
 * does not fit. */
static int layout(console_t *c, const term_font_t *font) {
    screen_t *s = c->screen;
    uint32_t cols = s->width / font->width, rows = s->height / font->height;
    uint32_t bars = (uint32_t)c->bars[0].shown + (uint32_t)c->bars[1].shown;
    if (rows <= bars || !term_resize(&c->term, cols, rows - bars)) {
        return -1;
    }
    c->render.y = c->bars[0].shown ? (int32_t)font->height : 0;
    for (int i = 0; i < 2; i++) {
        if (c->bars[i].shown) {
            term_resize(&c->bars[i].term, cols, 1);
            bar_text(c, i);
            c->bars[i].render.font = font;
            c->bars[i].render.y = i == CON_BAR_TOP ? 0
                                  : c->render.y + (int32_t)(c->term.rows * font->height);
        }
    }
    return 0;
}

/* Repaint the whole screen: blank what the cells do not reach, then the
 * bars and the grid. */
static void repaint(console_t *c) {
    screen_t *s = c->screen;
    for (unsigned y = 0; y < s->height; y++) {
        unsigned *row = (unsigned *)((char *)s->pixels + (size_t)y * s->pitch);
        for (unsigned x = 0; x < s->width; x++) {
            row[x] = TERM_DEFAULT_BG;
        }
    }
    for (int i = 0; i < 2; i++) {
        if (c->bars[i].shown) {
            term_render_all(&c->bars[i].render);
        }
    }
    c->render.cursor_drawn = false;
    term_render_all(&c->render);
    c->cursor_col = c->term.col;
    screen_damage(s, 0, 0, (int)s->width, (int)s->height);
}

static void *term_alloc(void *ctx, size_t bytes);
static void term_free(void *ctx, void *buffer);

/* Show bar `which` with `text`, or (NULL) take it away. 0, or -1 when the
 * grid would not fit or there is no memory. */
static int set_bar(console_t *c, int which, const char *text, size_t length) {
    if (which != CON_BAR_TOP && which != CON_BAR_BOTTOM) {
        return -1;
    }
    bool was = c->bars[which].shown;
    if (text == NULL) {
        if (!was) {
            return 0;
        }
        c->bars[which].shown = false;
        layout(c, c->render.font);  /* a row more always fits */
        free(c->bars[which].term.main_buf.cells);
        free(c->bars[which].term.alt_buf.cells);
        free(c->bars[which].text);
        c->bars[which].text = NULL;
        repaint(c);
        return 0;
    }
    char *copy = malloc(length + 1);  /* the sender's memory is lent until the reply */
    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, text, length);
    free(c->bars[which].text);
    c->bars[which].text = copy;
    c->bars[which].length = length;
    if (!was) {
        term_host_t host = {term_alloc, NULL, c, term_free};
        term_t *t = &c->bars[which].term;
        bool fits = term_init(t, c->term.cols, 1, 0, &host);
        c->bars[which].shown = fits;
        if (fits && layout(c, c->render.font) != 0) {
            c->bars[which].shown = fits = false;
            free(t->main_buf.cells);
            free(t->alt_buf.cells);
        }
        if (!fits) {
            free(copy);
            c->bars[which].text = NULL;
            return -1;
        }
        term_surface_t surface;
        screen_t *s = c->screen;
        term_surface_init(&surface, s->pixels, s->width, s->height, s->pitch);
        int32_t y = c->bars[which].render.y;
        term_render_init(&c->bars[which].render, t, &surface, 0, y);
        c->bars[which].render.font = c->render.font;
    }
    bar_text(c, which);
    if (!was) {
        repaint(c);
    }
    return 0;
}

int console_set_font(console_t *c, const void *psf, size_t size) {
    term_font_t font = term_font_builtin;
    void *data = NULL;
    if (psf != NULL) {
        /* The font is read from the console's own copy, its map after it:
         * the sender's memory is lent only until the reply. */
        long entries = term_font_psf2_entries(psf, size);
        size_t at = (size + 7) & ~(size_t)7;
        if (entries < 0 || (size_t)entries > (SIZE_MAX - at) / sizeof(term_font_entry_t)) {
            return -1;
        }
        data = malloc(at + (size_t)entries * sizeof(term_font_entry_t));
        if (data == NULL) {
            return -1;
        }
        memcpy(data, psf, size);
        if (term_font_psf2(&font, data, size, (term_font_entry_t *)((char *)data + at),
                           (size_t)entries) != 0) {
            free(data);
            return -1;
        }
    }
    if (layout(c, &font) != 0) {
        free(data);  /* larger than the screen, or no memory */
        return -1;
    }
    free(c->font_data);
    c->font_data = data;
    c->font = font;
    c->render.font = &c->font;
    for (int i = 0; i < 2; i++) {
        c->bars[i].render.font = &c->font;  /* layout() saw the caller's copy */
    }
    /* The cells may not reach the screen's right and bottom edges: blank
     * what they leave. */
    repaint(c);
    return 0;
}

console_t *console_open(screen_t *s) {
    console_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->screen = s;
    term_host_t host = {term_alloc, term_reply, c, term_free};
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

static void break_read(console_t *c);

/* CON_EVENTS: a key press for the readers (a modifier alone is none). The
 * oldest is dropped when the queue is full. */
static void queue_event(console_t *c, const struct input_event *event) {
    if (event->type != INPUT_EVENT_KEY_DOWN ||
        (event->keycode >= KEY_LEFT_CTRL && event->keycode <= KEY_RIGHT_GUI)) {
        return;
    }
    if (c->term.view_offset != 0) {
        term_scroll_view(&c->term, -(int)c->term.view_offset);  /* back to the live screen */
    }
    if (c->event_count == CONSOLE_EVENTS) {
        c->event_head = (c->event_head + 1) % CONSOLE_EVENTS;
        c->event_count--;
    }
    c->event_queue[(c->event_head + c->event_count) % CONSOLE_EVENTS] = (struct con_key_event){
        event->codepoint, event->keycode, event->modifiers};
    c->event_count++;
}

/* Into CON_EVENTS: what was typed and not read yet (lines entered, and the
 * line being typed in cooked mode) is not lost but becomes key presses, a
 * line's end an Enter, after the events still queued from before, so keys
 * typed while a program ran reach the program that reads events next
 * (KoraCLI's editor after a command). Text only: escape sequences and other
 * control bytes are dropped. */
static void typed_ahead_as_events(console_t *c) {
    con_edit_t *e = &c->edit;
    static char text[CON_READY_MAX + CON_LINE_MAX];  /* 2.5 KB: off the stack */
    size_t n = 0;
    memcpy(text, e->ready, e->ready_length);
    n = e->ready_length;
    if (e->mode == CON_COOKED && e->length > 0) {
        memcpy(text + n, e->line, e->length);
        n += e->length;
    }
    e->ready_length = 0;
    e->length = e->cursor = 0;
    e->editing = false;
    size_t i = 0;
    while (i < n) {
        unsigned char b = (unsigned char)text[i];
        uint32_t cp;
        if (b >= 0xF8) {  /* 0xFF: an end of input (Ctrl+D), no key */
            i++;
            continue;
        }
        size_t len = b < 0x80 ? 1 : b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (i + len > n) {
            break;  /* a character cut short */
        }
        cp = len == 1 ? b : (uint32_t)(b & (0x7F >> len));
        for (size_t k = 1; k < len; k++) {
            cp = (cp << 6) | ((unsigned char)text[i + k] & 0x3F);
        }
        i += len;
        struct input_event event = {.type = INPUT_EVENT_KEY_DOWN};
        if (cp == '\n' || cp == '\r') {
            event.keycode = KEY_ENTER;
        } else if (cp >= 0x20 && cp != 0x7F && (b < 0x80 || b >= 0xC0)) {
            event.keycode = KEY_A;  /* the character is what counts */
            event.codepoint = cp;
        } else {
            continue;
        }
        queue_event(c, &event);
    }
}

static void keys(console_t *c) {
    struct input_event event;
    while (screen_event(c->screen, &event)) {
        if (event.type == INPUT_EVENT_MOUSE) {
            /* The wheel scrolls the view, away from you back in time. */
            term_scroll_view(&c->term, event.wheel * CONSOLE_WHEEL_LINES);
            continue;
        }
        con_key_t key;
        con_key_bytes(event.type, event.modifiers, event.keycode, event.codepoint, &key);
        if (key.scroll != 0) {
            term_scroll_view(&c->term, key.scroll * (int)(c->term.rows / 2));
            continue;
        }
        if (event.type == INPUT_EVENT_KEY_DOWN && event.keycode == KEY_ESCAPE &&
            c->esc_break_pid > 0) {
            if (sig_send(c->esc_break_pid, SIG_MASK(SIG_BREAK)) == 0) {
                continue;  /* ESC is that program's break, not a key */
            }
            c->esc_break_pid = 0;  /* it has ended: ESC is a key again */
        }
        if (c->events && !(key.length == 1 && key.bytes[0] == 0x03)) {
            queue_event(c, &event);
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
        if (con_edit_break(&c->edit)) {
            if (c->on_break != NULL) {
                c->on_break(c->break_ctx);
            }
            break_read(c);
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
            if (packet->length == CON_EVENTS) {
                if (!c->events) {
                    typed_ahead_as_events(c);
                }
                c->events = true;
                con_edit_mode(&c->edit, CON_RAW);  /* typed bytes are not echoed */
            } else {
                /* Events not read yet stay for the next reader of events:
                 * they were typed for it (KoraCLI's editor, while a command
                 * runs in cooked mode). */
                c->events = false;
                con_edit_mode(&c->edit, (int)packet->length);
            }
            answer(packet, 0);
            break;
        case CON_BAR: {
            const struct con_bar *bar = packet->buf;
            answer(packet, packet->length == sizeof *bar
                               ? set_bar(c, bar->which, bar->text, bar->length) : -1);
            break;
        }
        case CON_ESC_BREAK:
            if (packet->length != 0) {
                c->esc_break_pid = got.sender;
            } else if (c->esc_break_pid == got.sender) {
                c->esc_break_pid = 0;
            }
            answer(packet, 0);
            break;
        case CON_FONT:
            answer(packet, console_set_font(c, packet->buf, packet->length));
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

/* Answer the oldest read with `result`, and stop watching its program when
 * it has no other read waiting. */
static void answer_oldest(console_t *c, long result) {
    answer(c->waiting[0].packet, result);
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

/* CON_EVENTS: the oldest reads get the events queued, as many as fit each. */
static void event_reads(console_t *c) {
    while (c->waiting_count > 0) {
        struct con_packet *packet = c->waiting[0].packet;
        size_t room = packet->length / sizeof(struct con_key_event);
        if (room == 0) {
            answer_oldest(c, -1);  /* no room for one */
            continue;
        }
        if (c->event_count == 0) {
            return;
        }
        struct con_key_event *out = packet->buf;
        size_t n = 0;
        while (n < room && c->event_count > 0) {
            out[n++] = c->event_queue[c->event_head];
            c->event_head = (c->event_head + 1) % CONSOLE_EVENTS;
            c->event_count--;
        }
        answer_oldest(c, (long)(n * sizeof *out));
    }
}

/* Answer the oldest reads while there is input for them. */
static void reads(console_t *c) {
    if (c->events) {
        event_reads(c);
        return;
    }
    while (c->waiting_count > 0) {
        struct con_packet *packet = c->waiting[0].packet;
        bool end;
        size_t n = con_edit_read(&c->edit, packet->buf, packet->length, &end);
        if (n == 0 && !end) {
            return;
        }
        answer_oldest(c, (long)n);
    }
}

/* Ctrl+C in cooked mode dropped the line being typed: the oldest read waiting
 * for a line ends with an empty one, after the break callback, so its program
 * goes on (the shell prints a new prompt) knowing of the break. A waiting
 * read never asks for 0 bytes (packets() answers those at once). */
static void break_read(console_t *c) {
    if (c->edit.mode != CON_COOKED || c->waiting_count == 0) {
        return;
    }
    ((char *)c->waiting[0].packet->buf)[0] = '\n';
    answer_oldest(c, 1);
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
    free(c->term.main_buf.cells);
    free(c->term.alt_buf.cells);
    for (int i = 0; i < 2; i++) {
        if (c->bars[i].shown) {
            free(c->bars[i].term.main_buf.cells);
            free(c->bars[i].term.alt_buf.cells);
        }
        free(c->bars[i].text);
    }
    free(c->font_data);
    free(c);
}
