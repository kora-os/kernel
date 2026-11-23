// SPDX-License-Identifier: GPL-3.0-or-later
//
// The console's line editor. See edit.h.
//
// The echo keeps the terminal's cursor where the editor thinks it is. A
// position in the line is a count of characters (UTF-8 code points, one
// cell each) from its start; with the column the line started in and the
// terminal's width it gives a row (relative) and a column. The cursor moves
// by rows (CSI A, CSI B) and to a column (CSI G), so it does not matter how
// far the screen scrolled. After writing up to the last column, xterm keeps
// the cursor there until the next character ("wrap pending"); a space and a
// carriage return put it at the start of the next row, where the editor
// counts it.

#include "libconsole/edit.h"

#define END_OF_INPUT ((char)0xFF)

static void out(con_edit_t *e, const char *bytes, size_t length) {
    if (length > 0) {
        e->echo(e->ctx, bytes, length);
    }
}

static void outs(con_edit_t *e, const char *text) {
    out(e, text, (size_t)strlen(text));
}

static void out_number(con_edit_t *e, const char *start, uint32_t n, char final) {
    char buf[16];
    size_t i = 0;
    while (*start) {
        buf[i++] = *start++;
    }
    char digits[10];
    size_t d = 0;
    do {
        digits[d++] = (char)('0' + n % 10);
        n /= 10;
    } while (n > 0);
    while (d > 0) {
        buf[i++] = digits[--d];
    }
    buf[i++] = final;
    out(e, buf, i);
}

static bool continuation(char c) {
    return ((unsigned char)c & 0xC0) == 0x80;
}

// Characters in line[0..index).
static uint32_t chars(const con_edit_t *e, size_t index) {
    uint32_t n = 0;
    for (size_t i = 0; i < index; i++) {
        n += !continuation(e->line[i]);
    }
    return n;
}

// The cell of position `index`, counted from the line's start.
static uint32_t cell(const con_edit_t *e, size_t index) {
    return e->start_col + chars(e, index);
}

// Move the terminal's cursor from position `from` to position `to`.
static void move(con_edit_t *e, size_t from, size_t to) {
    uint32_t a = cell(e, from), b = cell(e, to);
    uint32_t row_a = a / e->cols, row_b = b / e->cols;
    if (row_b < row_a) {
        out_number(e, "\x1B[", row_a - row_b, 'A');
    } else if (row_b > row_a) {
        out_number(e, "\x1B[", row_b - row_a, 'B');
    }
    if (a % e->cols != b % e->cols || row_a != row_b) {
        out_number(e, "\x1B[", b % e->cols + 1, 'G');
    }
}

// After writing up to position `index`: leave no wrap pending.
static void settle(con_edit_t *e, size_t index) {
    if (index > 0 && cell(e, index) % e->cols == 0) {
        outs(e, " \r");
    }
}

// The terminal's cursor is at `from`: redraw the line from there, clear what
// a longer line left, and put the cursor at the editor's.
static void redraw_from(con_edit_t *e, size_t from) {
    out(e, e->line + from, e->length - from);
    settle(e, e->length);
    outs(e, "\x1B[J");
    move(e, e->length, e->cursor);
}

static size_t previous(const con_edit_t *e, size_t i) {
    while (i > 0 && continuation(e->line[--i])) {
    }
    return i;
}

static size_t next(const con_edit_t *e, size_t i) {
    if (i < e->length) {
        i++;
    }
    while (i < e->length && continuation(e->line[i])) {
        i++;
    }
    return i;
}

static void start_line(con_edit_t *e) {
    if (!e->editing) {
        e->editing = true;
        e->browse = e->history_count;
    }
}

static void end_line(con_edit_t *e) {
    e->length = e->cursor = 0;
    e->editing = false;
    e->browse = e->history_count;
}

static void insert(con_edit_t *e, const char *bytes, size_t n) {
    start_line(e);
    if (e->length + n > CON_LINE_MAX - 1) {
        return;  // room for the '\n'
    }
    size_t from = e->cursor;
    memmove(e->line + from + n, e->line + from, e->length - from);
    memcpy(e->line + from, bytes, n);
    e->length += n;
    e->cursor += n;
    redraw_from(e, from);
}

static void remove_range(con_edit_t *e, size_t from, size_t to) {
    memmove(e->line + from, e->line + to, e->length - to);
    e->length -= to - from;
}

static void show(con_edit_t *e, const char *text, size_t length) {
    move(e, e->cursor, 0);
    memcpy(e->line, text, length);
    e->length = e->cursor = length;
    redraw_from(e, 0);
}

static void history_add(con_edit_t *e) {
    if (e->length == 0) {
        return;
    }
    if (e->history_count > 0) {
        const char *last = e->history[e->history_count - 1];
        if ((size_t)strlen(last) == e->length && strncmp(last, e->line, e->length) == 0) {
            return;
        }
    }
    if (e->history_count == CON_HISTORY) {
        memmove(e->history[0], e->history[1], sizeof(e->history[0]) * (CON_HISTORY - 1));
        e->history_count--;
    }
    memcpy(e->history[e->history_count], e->line, e->length);
    e->history[e->history_count][e->length] = '\0';
    e->history_count++;
}

static void browse(con_edit_t *e, int step) {
    start_line(e);
    if (step < 0 && e->browse == 0) {
        return;
    }
    if (step > 0 && e->browse == e->history_count) {
        return;
    }
    if (e->browse == e->history_count) {
        memcpy(e->draft, e->line, e->length);
        e->draft_length = e->length;
    }
    e->browse = step < 0 ? e->browse - 1 : e->browse + 1;
    if (e->browse == e->history_count) {
        show(e, e->draft, e->draft_length);
    } else {
        show(e, e->history[e->browse], (size_t)strlen(e->history[e->browse]));
    }
}

static void ready_add(con_edit_t *e, const char *bytes, size_t n) {
    if (e->ready_length + n <= CON_READY_MAX) {
        memcpy(e->ready + e->ready_length, bytes, n);
        e->ready_length += n;
    }
}

static void enter(con_edit_t *e) {
    start_line(e);
    move(e, e->cursor, e->length);
    outs(e, "\r\n");
    history_add(e);
    e->line[e->length] = '\n';
    ready_add(e, e->line, e->length + 1);
    if (e->entered != NULL) {
        e->entered(e->ctx, e->line, e->length + 1);
    }
    end_line(e);
}

static void interrupt(con_edit_t *e) {
    e->brk = true;
    if (e->mode == CON_COOKED) {
        if (e->editing) {
            move(e, e->cursor, e->length);
        }
        outs(e, "^C\r\n");
        if (e->entered != NULL) {
            e->entered(e->ctx, "^C\n", 3);
        }
        end_line(e);
    }
    e->ready_length = 0;  // what was typed ahead goes with the program
}

// A complete escape sequence in e->escape: cursor keys, Home, End, Delete.
static void escape(con_edit_t *e) {
    const char *s = e->escape + 1;
    size_t n = e->escape_length - 1;
    char final = s[n - 1];
    start_line(e);
    if (n == 2) {  // ESC [ x or ESC O x
        switch (final) {
        case 'A': browse(e, -1); return;
        case 'B': browse(e, 1); return;
        case 'C': move(e, e->cursor, next(e, e->cursor)); e->cursor = next(e, e->cursor); return;
        case 'D': {
            size_t to = previous(e, e->cursor);
            move(e, e->cursor, to);
            e->cursor = to;
            return;
        }
        case 'H': move(e, e->cursor, 0); e->cursor = 0; return;
        case 'F': move(e, e->cursor, e->length); e->cursor = e->length; return;
        default: return;
        }
    }
    if (n == 3 && s[0] == '[' && s[1] == '3' && final == '~' && e->cursor < e->length) {
        remove_range(e, e->cursor, next(e, e->cursor));
        redraw_from(e, e->cursor);
    }
}

static void cooked(con_edit_t *e, char c) {
    unsigned char u = (unsigned char)c;
    if (e->pending_need > 0) {
        if (!continuation(c)) {
            e->pending_need = 0;  // a broken character: dropped
        } else {
            e->pending[e->pending_length++] = c;
            if (e->pending_length == e->pending_need) {
                e->pending_need = 0;
                insert(e, e->pending, e->pending_length);
            }
            return;
        }
    }
    if (e->escape_length > 0) {
        e->escape[e->escape_length++] = c;
        if (e->escape_length == 2) {
            if (c != '[' && c != 'O') {
                e->escape_length = 0;  // Alt+key: ignored
            }
            return;
        }
        if (u >= 0x40 && u <= 0x7E) {
            escape(e);
            e->escape_length = 0;
        } else if (e->escape_length == sizeof(e->escape)) {
            e->escape_length = 0;
        }
        return;
    }
    if (u >= 0xC0 && u <= 0xF4) {
        e->pending[0] = c;
        e->pending_length = 1;
        e->pending_need = u >= 0xF0 ? 4 : u >= 0xE0 ? 3 : 2;
        return;
    }
    if (u >= 0x20 && u < 0x7F) {
        insert(e, &c, 1);
        return;
    }
    switch (u) {
    case 0x1B: e->escape[0] = c; e->escape_length = 1; return;
    case '\n': case '\r': enter(e); return;
    case 0x7F: case 0x08:
        if (e->cursor > 0) {
            size_t to = previous(e, e->cursor);
            move(e, e->cursor, to);
            remove_range(e, to, e->cursor);
            e->cursor = to;
            redraw_from(e, to);
        }
        return;
    case 0x01: start_line(e); move(e, e->cursor, 0); e->cursor = 0; return;           // ^A
    case 0x05: start_line(e); move(e, e->cursor, e->length); e->cursor = e->length; return;  // ^E
    case 0x15:  // ^U
        if (e->cursor > 0) {
            move(e, e->cursor, 0);
            remove_range(e, 0, e->cursor);
            e->cursor = 0;
            redraw_from(e, 0);
        }
        return;
    case 0x0B:  // ^K
        if (e->cursor < e->length) {
            e->length = e->cursor;
            outs(e, "\x1B[J");
        }
        return;
    case 0x04:  // ^D
        if (e->length == 0) {
            char end = END_OF_INPUT;
            ready_add(e, &end, 1);
            end_line(e);
        }
        return;
    default:
        return;  // Tab and the other control keys
    }
}

void con_edit_init(con_edit_t *e, void (*echo)(void *ctx, const char *bytes, size_t length),
                   void *ctx) {
    memset(e, 0, sizeof(*e));
    e->echo = echo;
    e->ctx = ctx;
    e->mode = CON_COOKED;
    e->cols = 80;
}

void con_edit_place(con_edit_t *e, uint32_t cols, uint32_t col) {
    if (!e->editing && cols > 0) {
        e->cols = cols;
        e->start_col = col < cols ? col : cols - 1;
    }
}

void con_edit_input(con_edit_t *e, const char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) {
        if (bytes[i] == 0x03) {
            e->escape_length = e->pending_need = 0;
            interrupt(e);
        } else if (e->mode == CON_RAW) {
            ready_add(e, bytes + i, 1);
        } else {
            cooked(e, bytes[i]);
        }
    }
    // A key's bytes come in one call: an ESC left alone was the Escape key.
    if (e->escape_length == 1) {
        e->escape_length = 0;
    }
}

void con_edit_mode(con_edit_t *e, int mode) {
    e->mode = mode == CON_RAW ? CON_RAW : CON_COOKED;
    e->escape_length = e->pending_need = 0;
}

size_t con_edit_read(con_edit_t *e, char *buf, size_t length, bool *end) {
    *end = false;
    if (e->ready_length == 0 || length == 0) {
        return 0;
    }
    if (e->ready[0] == END_OF_INPUT) {
        memmove(e->ready, e->ready + 1, --e->ready_length);
        *end = true;
        return 0;
    }
    size_t n = 0;
    while (n < length && n < e->ready_length && e->ready[n] != END_OF_INPUT) {
        buf[n] = e->ready[n];
        n++;
        if (e->mode == CON_COOKED && buf[n - 1] == '\n') {
            break;  // one line per read
        }
    }
    memmove(e->ready, e->ready + n, e->ready_length - n);
    e->ready_length -= n;
    return n;
}

bool con_edit_break(con_edit_t *e) {
    bool brk = e->brk;
    e->brk = false;
    return brk;
}
