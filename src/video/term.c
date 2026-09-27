// SPDX-License-Identifier: GPL-3.0-or-later
//
// Terminal model and xterm-compatible escape-sequence parser. See term.h.

#include "video/term.h"

#include "lib/string.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "tty.h"

// Pen-only attributes: applied when a glyph's colours are resolved, not drawn.
#define PEN_DIM 0x40
#define PEN_INVISIBLE 0x80
#define CELL_ATTR_MASK (TERM_ATTR_BOLD | TERM_ATTR_UNDERLINE | TERM_ATTR_REVERSE | TERM_ATTR_STRIKE)

enum {
    ST_GROUND,
    ST_ESC,
    ST_ESC_OTHER,  // ESC + intermediate we do not act on: skip to the final byte
    ST_CSI,
    ST_OSC,        // ESC ] ... BEL/ST: consumed and ignored (window titles etc.)
    ST_STRING,     // DCS/SOS/PM/APC ... ST: consumed and ignored
    ST_CHARSET,    // ESC ( X / ESC ) X
    ST_HASH,       // ESC # X
};

// --- Glyphs -------------------------------------------------------------------

// 8x8 line-drawing and symbol glyphs, MSB = leftmost pixel. Lines run through
// the middle two rows/columns so neighbouring cells join up.
static const uint8_t glyphs[TERM_GLYPH_END - 0x80][8] = {
    [TERM_GLYPH_HLINE - 0x80] = {0, 0, 0, 0xFF, 0xFF, 0, 0, 0},
    [TERM_GLYPH_VLINE - 0x80] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18},
    [TERM_GLYPH_ULCORNER - 0x80] = {0, 0, 0, 0x1F, 0x1F, 0x18, 0x18, 0x18},
    [TERM_GLYPH_URCORNER - 0x80] = {0, 0, 0, 0xF8, 0xF8, 0x18, 0x18, 0x18},
    [TERM_GLYPH_LLCORNER - 0x80] = {0x18, 0x18, 0x18, 0x1F, 0x1F, 0, 0, 0},
    [TERM_GLYPH_LRCORNER - 0x80] = {0x18, 0x18, 0x18, 0xF8, 0xF8, 0, 0, 0},
    [TERM_GLYPH_LTEE - 0x80] = {0x18, 0x18, 0x18, 0x1F, 0x1F, 0x18, 0x18, 0x18},
    [TERM_GLYPH_RTEE - 0x80] = {0x18, 0x18, 0x18, 0xF8, 0xF8, 0x18, 0x18, 0x18},
    [TERM_GLYPH_TTEE - 0x80] = {0, 0, 0, 0xFF, 0xFF, 0x18, 0x18, 0x18},
    [TERM_GLYPH_BTEE - 0x80] = {0x18, 0x18, 0x18, 0xFF, 0xFF, 0, 0, 0},
    [TERM_GLYPH_CROSS - 0x80] = {0x18, 0x18, 0x18, 0xFF, 0xFF, 0x18, 0x18, 0x18},
    [TERM_GLYPH_CHECKER - 0x80] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},
    [TERM_GLYPH_BLOCK - 0x80] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    [TERM_GLYPH_DIAMOND - 0x80] = {0, 0x18, 0x3C, 0x7E, 0x3C, 0x18, 0, 0},
    [TERM_GLYPH_BULLET - 0x80] = {0, 0, 0x18, 0x3C, 0x3C, 0x18, 0, 0},
    [TERM_GLYPH_DEGREE - 0x80] = {0x30, 0x48, 0x48, 0x30, 0, 0, 0, 0},
    [TERM_GLYPH_PLUSMINUS - 0x80] = {0x18, 0x18, 0x7E, 0x18, 0x18, 0, 0x7E, 0},
};

const uint8_t *term_glyph(uint8_t code) {
    if (code < 0x80 || code >= TERM_GLYPH_END) {
        return glyphs[0];  // not reached for valid codes
    }
    return glyphs[code - 0x80];
}

// DEC special graphics (ESC ( 0) for 0x5F..0x7E. Symbols the font lacks fall
// back to a close ASCII character.
static const uint8_t dec_graphics[0x7F - 0x5F] = {
    ' ',                  // _ blank
    TERM_GLYPH_DIAMOND,   // `
    TERM_GLYPH_CHECKER,   // a
    '?', '?', '?', '?',   // b c d e: HT FF CR LF symbols
    TERM_GLYPH_DEGREE,    // f
    TERM_GLYPH_PLUSMINUS, // g
    '?', '?',             // h i: NL VT symbols
    TERM_GLYPH_LRCORNER,  // j
    TERM_GLYPH_URCORNER,  // k
    TERM_GLYPH_ULCORNER,  // l
    TERM_GLYPH_LLCORNER,  // m
    TERM_GLYPH_CROSS,     // n
    TERM_GLYPH_HLINE,     // o scan line 1
    TERM_GLYPH_HLINE,     // p scan line 3
    TERM_GLYPH_HLINE,     // q scan line 5 (horizontal line)
    TERM_GLYPH_HLINE,     // r scan line 7
    TERM_GLYPH_HLINE,     // s scan line 9
    TERM_GLYPH_LTEE,      // t
    TERM_GLYPH_RTEE,      // u
    TERM_GLYPH_BTEE,      // v
    TERM_GLYPH_TTEE,      // w
    TERM_GLYPH_VLINE,     // x
    '<', '>',             // y z: less/greater-or-equal
    '?', '#', 'L',        // { | }: pi, not-equal, pound
    TERM_GLYPH_BULLET,    // ~
};

// A decoded Unicode code point -> glyph. Box drawing (light, heavy, double and
// rounded variants all map to the single-line glyphs), blocks and a few
// symbols; anything else outside ASCII shows as '?'.
static uint8_t unicode_glyph(uint32_t cp) {
    if (cp < 0x80) {
        return (uint8_t)cp;
    }
    if (cp >= 0x2500 && cp <= 0x257F) {
        // Straight lines: solid, dashed (2504..250B and 254C..254F come in
        // horizontal/vertical pairs) and the half lines 2574..257F (even =
        // horizontal).
        if (cp <= 0x2501 || cp == 0x2550 || cp == 0x254C || cp == 0x254D ||
            (cp >= 0x2504 && cp <= 0x250B && ((cp - 0x2504) & 2) == 0) ||
            (cp >= 0x2574 && (cp & 1) == 0)) {
            return TERM_GLYPH_HLINE;
        }
        if (cp <= 0x250B || cp == 0x2551 || cp == 0x254E || cp == 0x254F || cp >= 0x2574) {
            return TERM_GLYPH_VLINE;
        }
        if ((cp >= 0x250C && cp <= 0x250F) || (cp >= 0x2552 && cp <= 0x2554) || cp == 0x256D) {
            return TERM_GLYPH_ULCORNER;
        }
        if ((cp >= 0x2510 && cp <= 0x2513) || (cp >= 0x2555 && cp <= 0x2557) || cp == 0x256E) {
            return TERM_GLYPH_URCORNER;
        }
        if ((cp >= 0x2514 && cp <= 0x2517) || (cp >= 0x2558 && cp <= 0x255A) || cp == 0x2570) {
            return TERM_GLYPH_LLCORNER;
        }
        if ((cp >= 0x2518 && cp <= 0x251B) || (cp >= 0x255B && cp <= 0x255D) || cp == 0x256F) {
            return TERM_GLYPH_LRCORNER;
        }
        if ((cp >= 0x251C && cp <= 0x2523) || (cp >= 0x255E && cp <= 0x2560)) {
            return TERM_GLYPH_LTEE;
        }
        if ((cp >= 0x2524 && cp <= 0x252B) || (cp >= 0x2561 && cp <= 0x2563)) {
            return TERM_GLYPH_RTEE;
        }
        if ((cp >= 0x252C && cp <= 0x2533) || (cp >= 0x2564 && cp <= 0x2566)) {
            return TERM_GLYPH_TTEE;
        }
        if ((cp >= 0x2534 && cp <= 0x253B) || (cp >= 0x2567 && cp <= 0x2569)) {
            return TERM_GLYPH_BTEE;
        }
        if ((cp >= 0x253C && cp <= 0x254B) || (cp >= 0x256A && cp <= 0x256C)) {
            return TERM_GLYPH_CROSS;
        }
        return '?';
    }
    switch (cp) {
    case 0x00A0: return ' ';
    case 0x00B0: return TERM_GLYPH_DEGREE;
    case 0x00B1: return TERM_GLYPH_PLUSMINUS;
    case 0x00B7: case 0x2022: case 0x25CF: return TERM_GLYPH_BULLET;
    case 0x2588: return TERM_GLYPH_BLOCK;
    case 0x2591: case 0x2592: case 0x2593: return TERM_GLYPH_CHECKER;
    case 0x25C6: case 0x2666: return TERM_GLYPH_DIAMOND;
    default: return '?';
    }
}

// --- Colours ------------------------------------------------------------------

// xterm's default 16 colours, then the 6x6x6 cube and the grey ramp.
static const uint32_t base_palette[16] = {
    0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
    0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff,
};

static uint32_t palette(uint32_t i) {
    if (i < 16) {
        return base_palette[i];
    }
    if (i < 232) {
        static const uint8_t level[6] = {0, 95, 135, 175, 215, 255};
        i -= 16;
        return ((uint32_t)level[i / 36] << 16) | ((uint32_t)level[(i / 6) % 6] << 8) | level[i % 6];
    }
    uint32_t g = 8 + (i - 232) * 10;
    return (g << 16) | (g << 8) | g;
}

static uint32_t resolve(term_color_t c, bool fg, uint8_t attr) {
    switch (c.kind) {
    case TERM_COLOR_INDEX: {
        uint32_t idx = c.value;
        if (fg && (attr & TERM_ATTR_BOLD) && idx < 8) {
            idx += 8;  // bold brightens the basic colours, as xterm does
        }
        return palette(idx);
    }
    case TERM_COLOR_RGB:
        return c.value;
    default:
        return fg ? TERM_DEFAULT_FG : TERM_DEFAULT_BG;
    }
}

static void pen_colors(const term_t *t, uint32_t *fg, uint32_t *bg) {
    *fg = resolve(t->pen.fg, true, t->pen.attr);
    *bg = resolve(t->pen.bg, false, t->pen.attr);
    if (t->pen.attr & PEN_DIM) {
        *fg = (*fg >> 1) & 0x7f7f7f;
    }
    if (t->pen.attr & PEN_INVISIBLE) {
        *fg = *bg;
    }
}

// Erased cells take the current background colour (xterm's "bce").
static term_cell_t blank_cell(const term_t *t) {
    term_cell_t c;
    c.fg = resolve(t->pen.fg, true, 0);
    c.bg = resolve(t->pen.bg, false, 0);
    c.ch = ' ';
    c.attr = 0;
    return c;
}

// --- Buffers and dirty tracking ----------------------------------------------

static void mark_row(term_t *t, uint32_t row) {
    if (row < t->rows) {
        t->dirty[row] = true;
    }
}

static void mark_all(term_t *t) {
    for (uint32_t r = 0; r < t->rows; r++) {
        t->dirty[r] = true;
    }
}

// Ring line for a screen row of `b` (negative rows reach into history).
static term_cell_t *buffer_line(const term_t *t, const term_buffer_t *b, int32_t row) {
    int64_t idx = ((int64_t)b->top + row) % (int64_t)b->total_lines;
    if (idx < 0) {
        idx += b->total_lines;
    }
    return b->cells + (uint64_t)idx * t->cols;
}

static term_cell_t *line(term_t *t, uint32_t row) {
    return buffer_line(t, t->buf, (int32_t)row);
}

static void blank_range(term_t *t, uint32_t row, uint32_t from, uint32_t to) {
    term_cell_t blank = blank_cell(t);
    term_cell_t *l = line(t, row);
    for (uint32_t c = from; c < to && c < t->cols; c++) {
        l[c] = blank;
    }
    mark_row(t, row);
}

static void blank_line(term_t *t, uint32_t row) {
    blank_range(t, row, 0, t->cols);
}

// Scroll rows [top, bottom] up by n. With `save` and the whole screen as the
// region, the ring just rotates: the lines leaving the top become history.
static void scroll_up(term_t *t, uint32_t top, uint32_t bottom, uint32_t n, bool save) {
    uint32_t height = bottom - top + 1;
    if (n > height) {
        n = height;
    }
    term_buffer_t *b = t->buf;
    if (save && top == 0 && bottom == t->rows - 1) {
        uint32_t capacity = b->total_lines - t->rows;
        for (uint32_t i = 0; i < n; i++) {
            b->top = (b->top + 1) % b->total_lines;
            if (b->history < capacity) {
                b->history++;
            }
            blank_line(t, t->rows - 1);
        }
        mark_all(t);
        return;
    }
    for (uint32_t r = top; r + n <= bottom; r++) {
        memcpy(line(t, r), line(t, r + n), t->cols * sizeof(term_cell_t));
        mark_row(t, r);
    }
    for (uint32_t r = bottom + 1 - n; r <= bottom; r++) {
        blank_line(t, r);
    }
}

// Scroll rows [top, bottom] down by n (blank lines appear at the top).
static void scroll_down(term_t *t, uint32_t top, uint32_t bottom, uint32_t n) {
    uint32_t height = bottom - top + 1;
    if (n > height) {
        n = height;
    }
    for (uint32_t r = bottom; r >= top + n; r--) {
        memcpy(line(t, r), line(t, r - n), t->cols * sizeof(term_cell_t));
        mark_row(t, r);
    }
    for (uint32_t r = top; r < top + n; r++) {
        blank_line(t, r);
    }
}

// --- Cursor -------------------------------------------------------------------

static uint32_t min_row(const term_t *t) {
    return t->origin_mode ? t->margin_top : 0;
}

static uint32_t max_row(const term_t *t) {
    return t->origin_mode ? t->margin_bottom : t->rows - 1;
}

static void move_to(term_t *t, uint32_t col, uint32_t row) {
    t->col = col < t->cols ? col : t->cols - 1;
    if (row < min_row(t)) {
        row = min_row(t);
    }
    t->row = row > max_row(t) ? max_row(t) : row;
    t->wrap_pending = false;
}

// LF / IND: down a line, scrolling the region at its bottom margin.
static void index_down(term_t *t) {
    t->wrap_pending = false;
    if (t->row == t->margin_bottom) {
        scroll_up(t, t->margin_top, t->margin_bottom, 1, true);
    } else if (t->row < t->rows - 1) {
        t->row++;
    }
}

// RI: up a line, scrolling the region down at its top margin.
static void reverse_index(term_t *t) {
    t->wrap_pending = false;
    if (t->row == t->margin_top) {
        scroll_down(t, t->margin_top, t->margin_bottom, 1);
    } else if (t->row > 0) {
        t->row--;
    }
}

static void tab_forward(term_t *t, uint32_t n) {
    t->wrap_pending = false;
    while (n-- > 0 && t->col < t->cols - 1) {
        do {
            t->col++;
        } while (t->col < t->cols - 1 && !t->tab_stops[t->col]);
    }
}

static void tab_backward(term_t *t, uint32_t n) {
    t->wrap_pending = false;
    while (n-- > 0 && t->col > 0) {
        do {
            t->col--;
        } while (t->col > 0 && !t->tab_stops[t->col]);
    }
}

static void default_saved(term_saved_cursor_t *s) {
    memset(s, 0, sizeof(*s));
    s->charset[0] = s->charset[1] = 'B';
}

static void save_cursor(term_t *t) {
    term_saved_cursor_t *s = &t->buf->saved;
    s->col = t->col;
    s->row = t->row;
    s->wrap_pending = t->wrap_pending;
    s->origin_mode = t->origin_mode;
    s->pen = t->pen;
    s->charset[0] = t->charset[0];
    s->charset[1] = t->charset[1];
    s->gl = t->gl;
}

static void restore_cursor(term_t *t) {
    const term_saved_cursor_t *s = &t->buf->saved;
    t->origin_mode = s->origin_mode;
    t->pen = s->pen;
    t->charset[0] = s->charset[0];
    t->charset[1] = s->charset[1];
    t->gl = s->gl;
    t->col = s->col < t->cols ? s->col : t->cols - 1;
    t->row = s->row < t->rows ? s->row : t->rows - 1;
    t->wrap_pending = s->wrap_pending;
}

// --- Printing -----------------------------------------------------------------

static void put_glyph(term_t *t, uint8_t glyph) {
    if (t->wrap_pending) {
        t->col = 0;
        index_down(t);
    }
    term_cell_t *l = line(t, t->row);
    if (t->insert_mode && t->col < t->cols - 1) {
        memmove(&l[t->col + 1], &l[t->col], (t->cols - t->col - 1) * sizeof(term_cell_t));
    }
    term_cell_t *cell = &l[t->col];
    pen_colors(t, &cell->fg, &cell->bg);
    cell->ch = glyph;
    cell->attr = t->pen.attr & CELL_ATTR_MASK;
    mark_row(t, t->row);
    t->last_glyph = glyph;

    if (t->col == t->cols - 1) {
        t->wrap_pending = t->autowrap;
    } else {
        t->col++;
    }
}

static void print_ascii(term_t *t, uint8_t c) {
    if (t->charset[t->gl] == '0' && c >= 0x5F && c <= 0x7E) {
        put_glyph(t, dec_graphics[c - 0x5F]);
    } else {
        put_glyph(t, c);
    }
}

// --- Replies (status reports go back as keyboard input) -----------------------

static void reply(const char *s) {
    while (*s) {
        tty_input_push(*s++);
    }
}

static char *append_uint(char *p, uint32_t v) {
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (n > 0) {
        *p++ = tmp[--n];
    }
    return p;
}

// Reply "ESC [ <prefix> a ; b <final>".
static void reply_pair(const char *prefix, uint32_t a, uint32_t b, char final) {
    char buf[40];
    char *p = buf;
    *p++ = 0x1B;
    *p++ = '[';
    while (*prefix) {
        *p++ = *prefix++;
    }
    p = append_uint(p, a);
    *p++ = ';';
    p = append_uint(p, b);
    *p++ = final;
    *p = '\0';
    reply(buf);
}

// --- Modes and resets ---------------------------------------------------------

static void switch_screen(term_t *t, bool alt) {
    term_buffer_t *want = alt ? &t->alt_buf : &t->main_buf;
    if (t->buf != want) {
        t->buf = want;
        t->view_offset = 0;
        mark_all(t);
    }
}

static void clear_screen(term_t *t) {
    for (uint32_t r = 0; r < t->rows; r++) {
        blank_line(t, r);
    }
}

static void soft_reset(term_t *t) {
    t->cursor_visible = true;
    t->insert_mode = false;
    t->origin_mode = false;
    t->autowrap = true;
    t->margin_top = 0;
    t->margin_bottom = t->rows - 1;
    memset(&t->pen, 0, sizeof(t->pen));
    t->charset[0] = t->charset[1] = 'B';
    t->gl = 0;
    t->wrap_pending = false;
    default_saved(&t->main_buf.saved);
    default_saved(&t->alt_buf.saved);
}

static void full_reset(term_t *t) {
    soft_reset(t);
    t->newline_mode = false;
    t->cursor_style = TERM_DEFAULT_CURSOR;
    for (uint32_t c = 0; c < TERM_MAX_COLS; c++) {
        t->tab_stops[c] = (c % 8) == 0 && c != 0;
    }
    switch_screen(t, false);
    clear_screen(t);
    t->col = 0;
    t->row = 0;
    t->state = ST_GROUND;
    t->utf8_left = 0;
}

static void set_private_mode(term_t *t, uint32_t mode, bool on) {
    switch (mode) {
    case 6:  // DECOM
        t->origin_mode = on;
        move_to(t, 0, min_row(t));
        break;
    case 7:  // DECAWM
        t->autowrap = on;
        if (!on) {
            t->wrap_pending = false;
        }
        break;
    case 25:  // DECTCEM
        t->cursor_visible = on;
        mark_row(t, t->row);
        break;
    case 47:  // alternate screen
        switch_screen(t, on);
        break;
    case 1047:  // alternate screen, cleared when leaving it
        if (!on && t->buf == &t->alt_buf) {
            clear_screen(t);
        }
        switch_screen(t, on);
        break;
    case 1048:
        if (on) {
            save_cursor(t);
        } else {
            restore_cursor(t);
        }
        break;
    case 1049:  // save cursor + alternate screen (cleared on entry)
        if (on) {
            save_cursor(t);
            switch_screen(t, true);
            clear_screen(t);
        } else {
            switch_screen(t, false);
            restore_cursor(t);
        }
        break;
    default:
        // 1 (DECCKM), 12 (blink), 2004 (bracketed paste), mouse modes, ...:
        // accepted and ignored.
        break;
    }
}

// --- CSI dispatch -------------------------------------------------------------

// Parameter i, or `def` if absent or zero (the usual VT convention).
static uint32_t param(const term_t *t, uint32_t i, uint32_t def) {
    return (i < t->nparams && t->params[i] != 0) ? t->params[i] : def;
}

// SGR 38/48 extended colour at params[*i]: "5;n" or "2;r;g;b" (also the
// "2::r:g:b" colon form with an empty colour-space id).
static bool sgr_extended(term_t *t, uint32_t *i, term_color_t *out) {
    if (*i + 1 >= t->nparams) {
        return false;
    }
    uint32_t kind = t->params[*i + 1];
    if (kind == 5 && *i + 2 < t->nparams) {
        out->kind = TERM_COLOR_INDEX;
        out->value = t->params[*i + 2] & 0xFF;
        *i += 2;
        return true;
    }
    if (kind == 2) {
        uint32_t base = *i + 2;
        if (t->colon && base + 3 < t->nparams) {
            base++;  // skip the colour-space id
        }
        if (base + 2 < t->nparams) {
            uint32_t r = t->params[base] > 255 ? 255 : t->params[base];
            uint32_t g = t->params[base + 1] > 255 ? 255 : t->params[base + 1];
            uint32_t b = t->params[base + 2] > 255 ? 255 : t->params[base + 2];
            out->kind = TERM_COLOR_RGB;
            out->value = (r << 16) | (g << 8) | b;
            *i = base + 2;
            return true;
        }
    }
    *i = t->nparams;  // malformed: drop the rest
    return false;
}

static void sgr(term_t *t) {
    for (uint32_t i = 0; i < t->nparams; i++) {
        uint32_t p = t->params[i];
        term_pen_t *pen = &t->pen;
        if (p == 0) {
            memset(pen, 0, sizeof(*pen));
        } else if (p == 1) {
            pen->attr |= TERM_ATTR_BOLD;
        } else if (p == 2) {
            pen->attr |= PEN_DIM;
        } else if (p == 4 || p == 21) {
            pen->attr |= TERM_ATTR_UNDERLINE;
        } else if (p == 7) {
            pen->attr |= TERM_ATTR_REVERSE;
        } else if (p == 8) {
            pen->attr |= PEN_INVISIBLE;
        } else if (p == 9) {
            pen->attr |= TERM_ATTR_STRIKE;
        } else if (p == 22) {
            pen->attr &= ~(TERM_ATTR_BOLD | PEN_DIM);
        } else if (p == 24) {
            pen->attr &= ~TERM_ATTR_UNDERLINE;
        } else if (p == 27) {
            pen->attr &= ~TERM_ATTR_REVERSE;
        } else if (p == 28) {
            pen->attr &= ~PEN_INVISIBLE;
        } else if (p == 29) {
            pen->attr &= ~TERM_ATTR_STRIKE;
        } else if (p >= 30 && p <= 37) {
            pen->fg.kind = TERM_COLOR_INDEX;
            pen->fg.value = p - 30;
        } else if (p == 38) {
            sgr_extended(t, &i, &pen->fg);
        } else if (p == 39) {
            pen->fg.kind = TERM_COLOR_DEFAULT;
        } else if (p >= 40 && p <= 47) {
            pen->bg.kind = TERM_COLOR_INDEX;
            pen->bg.value = p - 40;
        } else if (p == 48) {
            sgr_extended(t, &i, &pen->bg);
        } else if (p == 49) {
            pen->bg.kind = TERM_COLOR_DEFAULT;
        } else if (p >= 90 && p <= 97) {
            pen->fg.kind = TERM_COLOR_INDEX;
            pen->fg.value = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            pen->bg.kind = TERM_COLOR_INDEX;
            pen->bg.value = p - 100 + 8;
        }
        // 3 (italic), 5/6 (blink), 23, 25, ...: no rendering; ignored.
    }
}

static void erase_display(term_t *t, uint32_t mode) {
    switch (mode) {
    case 0:
        blank_range(t, t->row, t->col, t->cols);
        for (uint32_t r = t->row + 1; r < t->rows; r++) {
            blank_line(t, r);
        }
        break;
    case 1:
        for (uint32_t r = 0; r < t->row; r++) {
            blank_line(t, r);
        }
        blank_range(t, t->row, 0, t->col + 1);
        break;
    case 2:
        clear_screen(t);
        break;
    case 3:  // xterm: erase the scrollback
        t->main_buf.history = 0;
        t->view_offset = 0;
        mark_all(t);
        break;
    }
}

static void csi_dispatch(term_t *t, uint8_t final) {
    if (t->private_marker == '?') {
        if (final == 'h' || final == 'l') {
            for (uint32_t i = 0; i < t->nparams; i++) {
                set_private_mode(t, t->params[i], final == 'h');
            }
        }
        return;
    }
    if (t->private_marker == '>') {
        if (final == 'c') {
            reply("\x1b[>0;0;0c");  // secondary device attributes
        }
        return;
    }
    if (t->private_marker != 0) {
        return;
    }
    if (t->intermediate == ' ') {
        if (final == 'q') {  // DECSCUSR
            uint32_t s = param(t, 0, 0);
            t->cursor_style = s == 0 ? TERM_DEFAULT_CURSOR
                            : s <= 2 ? TERM_CURSOR_BLOCK
                            : s <= 4 ? TERM_CURSOR_UNDERLINE
                                     : TERM_CURSOR_BAR;
            mark_row(t, t->row);
        }
        return;
    }
    if (t->intermediate == '!') {
        if (final == 'p') {  // DECSTR
            soft_reset(t);
        }
        return;
    }
    if (t->intermediate != 0) {
        return;
    }

    uint32_t n = param(t, 0, 1);
    switch (final) {
    case '@': {  // ICH
        uint32_t room = t->cols - t->col;
        if (n > room) {
            n = room;
        }
        term_cell_t *l = line(t, t->row);
        memmove(&l[t->col + n], &l[t->col], (room - n) * sizeof(term_cell_t));
        blank_range(t, t->row, t->col, t->col + n);
        t->wrap_pending = false;
        break;
    }
    case 'A': {  // CUU
        uint32_t limit = t->row >= t->margin_top ? t->margin_top : 0;
        t->row = t->row - limit > n ? t->row - n : limit;
        t->wrap_pending = false;
        break;
    }
    case 'B':    // CUD
    case 'e': {  // VPR
        uint32_t limit = t->row <= t->margin_bottom ? t->margin_bottom : t->rows - 1;
        t->row = t->row + n < limit ? t->row + n : limit;
        t->wrap_pending = false;
        break;
    }
    case 'C':  // CUF
    case 'a':  // HPR
        move_to(t, t->col + n, t->row);
        break;
    case 'D':  // CUB
        move_to(t, t->col > n ? t->col - n : 0, t->row);
        break;
    case 'E':  // CNL
        t->col = 0;
        csi_dispatch(t, 'B');
        break;
    case 'F':  // CPL
        t->col = 0;
        csi_dispatch(t, 'A');
        break;
    case 'G':  // CHA
    case '`':  // HPA
        move_to(t, n - 1, t->row);
        break;
    case 'H':  // CUP
    case 'f':  // HVP
        move_to(t, param(t, 1, 1) - 1, min_row(t) + n - 1);
        break;
    case 'I':  // CHT
        tab_forward(t, n);
        break;
    case 'J':  // ED
        erase_display(t, param(t, 0, 0));
        break;
    case 'K':  // EL
        switch (param(t, 0, 0)) {
        case 0: blank_range(t, t->row, t->col, t->cols); break;
        case 1: blank_range(t, t->row, 0, t->col + 1); break;
        case 2: blank_line(t, t->row); break;
        }
        break;
    case 'L':  // IL
        if (t->row >= t->margin_top && t->row <= t->margin_bottom) {
            scroll_down(t, t->row, t->margin_bottom, n);
            t->col = 0;
            t->wrap_pending = false;
        }
        break;
    case 'M':  // DL
        if (t->row >= t->margin_top && t->row <= t->margin_bottom) {
            scroll_up(t, t->row, t->margin_bottom, n, false);
            t->col = 0;
            t->wrap_pending = false;
        }
        break;
    case 'P': {  // DCH
        uint32_t room = t->cols - t->col;
        if (n > room) {
            n = room;
        }
        term_cell_t *l = line(t, t->row);
        memmove(&l[t->col], &l[t->col + n], (room - n) * sizeof(term_cell_t));
        blank_range(t, t->row, t->cols - n, t->cols);
        t->wrap_pending = false;
        break;
    }
    case 'S':  // SU
        scroll_up(t, t->margin_top, t->margin_bottom, n, true);
        break;
    case 'T':  // SD (with more than one parameter it is mouse tracking)
        if (t->nparams <= 1) {
            scroll_down(t, t->margin_top, t->margin_bottom, n);
        }
        break;
    case 'X':  // ECH
        blank_range(t, t->row, t->col, t->col + n);
        t->wrap_pending = false;
        break;
    case 'Z':  // CBT
        tab_backward(t, n);
        break;
    case 'b':  // REP
        if (t->last_glyph != 0) {
            uint32_t max = t->cols * t->rows;
            for (uint32_t i = 0; i < n && i < max; i++) {
                put_glyph(t, t->last_glyph);
            }
        }
        break;
    case 'c':  // DA
        if (param(t, 0, 0) == 0) {
            reply("\x1b[?62;22c");  // VT220 with ANSI colour
        }
        break;
    case 'd':  // VPA
        move_to(t, t->col, min_row(t) + n - 1);
        break;
    case 'g':  // TBC
        if (param(t, 0, 0) == 0) {
            t->tab_stops[t->col] = false;
        } else if (param(t, 0, 0) == 3) {
            memset(t->tab_stops, 0, sizeof(t->tab_stops));
        }
        break;
    case 'h':
    case 'l':
        for (uint32_t i = 0; i < t->nparams; i++) {
            if (t->params[i] == 4) {
                t->insert_mode = final == 'h';  // IRM
            } else if (t->params[i] == 20) {
                t->newline_mode = final == 'h';  // LNM
            }
        }
        break;
    case 'm':
        sgr(t);
        break;
    case 'n':  // DSR
        if (param(t, 0, 0) == 5) {
            reply("\x1b[0n");
        } else if (param(t, 0, 0) == 6) {
            reply_pair("", t->row - min_row(t) + 1, t->col + 1, 'R');
        }
        break;
    case 'r': {  // DECSTBM
        uint32_t top = param(t, 0, 1);
        uint32_t bottom = param(t, 1, t->rows);
        if (top < bottom && bottom <= t->rows) {
            t->margin_top = top - 1;
            t->margin_bottom = bottom - 1;
            move_to(t, 0, min_row(t));
        }
        break;
    }
    case 's':  // SCOSC
        save_cursor(t);
        break;
    case 'u':  // SCORC
        restore_cursor(t);
        break;
    case 't':  // window operations: only the size reports
        if (param(t, 0, 0) == 18) {
            reply_pair("8;", t->rows, t->cols, 't');
        } else if (param(t, 0, 0) == 14) {
            reply_pair("4;", t->rows * 8, t->cols * 8, 't');
        }
        break;
    default:
        break;
    }
}

// --- ESC dispatch -------------------------------------------------------------

static void esc_dispatch(term_t *t, uint8_t b) {
    t->state = ST_GROUND;
    switch (b) {
    case '[':
        t->state = ST_CSI;
        memset(t->params, 0, sizeof(t->params));
        t->nparams = 1;
        t->private_marker = 0;
        t->intermediate = 0;
        t->colon = false;
        break;
    case ']':
        t->state = ST_OSC;
        break;
    case 'P':
    case 'X':
    case '^':
    case '_':
        t->state = ST_STRING;
        break;
    case '(':
    case ')':
        t->state = ST_CHARSET;
        t->charset_slot = b == '(' ? 0 : 1;
        break;
    case '*':
    case '+':
        t->state = ST_CHARSET;
        t->charset_slot = 0xFF;  // G2/G3: parsed, not used
        break;
    case '#':
        t->state = ST_HASH;
        break;
    case '7':
        save_cursor(t);
        break;
    case '8':
        restore_cursor(t);
        break;
    case 'D':
        index_down(t);
        break;
    case 'E':
        t->col = 0;
        index_down(t);
        break;
    case 'M':
        reverse_index(t);
        break;
    case 'H':
        t->tab_stops[t->col] = true;
        break;
    case 'c':
        full_reset(t);
        break;
    default:
        if (b >= 0x20 && b <= 0x2F) {
            t->state = ST_ESC_OTHER;
        }
        // '=' / '>' (keypad modes), '\' (string terminator), ...: ignored.
        break;
    }
}

// --- Input --------------------------------------------------------------------

static void control(term_t *t, uint8_t b) {
    switch (b) {
    case 0x08:  // BS
        if (t->col > 0) {
            t->col--;
        }
        t->wrap_pending = false;
        break;
    case 0x09:  // HT
        tab_forward(t, 1);
        break;
    case 0x0A:  // LF
    case 0x0B:  // VT
    case 0x0C:  // FF
        index_down(t);
        if (t->newline_mode) {
            t->col = 0;
        }
        break;
    case 0x0D:  // CR
        t->col = 0;
        t->wrap_pending = false;
        break;
    case 0x0E:  // SO
        t->gl = 1;
        break;
    case 0x0F:  // SI
        t->gl = 0;
        break;
    default:
        break;  // BEL and the rest: ignored
    }
}

static void ground(term_t *t, uint8_t b) {
    if (b < 0x80) {
        if (t->utf8_left != 0) {
            t->utf8_left = 0;
            put_glyph(t, '?');  // truncated UTF-8 sequence
        }
        print_ascii(t, b);
        return;
    }
    if (t->utf8_left != 0 && (b & 0xC0) == 0x80) {
        t->utf8_cp = (t->utf8_cp << 6) | (b & 0x3F);
        if (--t->utf8_left == 0) {
            put_glyph(t, unicode_glyph(t->utf8_cp));
        }
        return;
    }
    if (t->utf8_left != 0) {
        t->utf8_left = 0;
        put_glyph(t, '?');
    }
    if (b >= 0xC2 && b <= 0xDF) {
        t->utf8_cp = b & 0x1F;
        t->utf8_left = 1;
    } else if (b >= 0xE0 && b <= 0xEF) {
        t->utf8_cp = b & 0x0F;
        t->utf8_left = 2;
    } else if (b >= 0xF0 && b <= 0xF4) {
        t->utf8_cp = b & 0x07;
        t->utf8_left = 3;
    } else {
        put_glyph(t, '?');  // stray continuation or invalid byte
    }
}

void term_putc(term_t *t, char ch) {
    uint8_t b = (uint8_t)ch;

    // New output returns a scrolled-back view to the live screen.
    if (t->view_offset != 0) {
        t->view_offset = 0;
        mark_all(t);
    }

    if (t->state == ST_OSC || t->state == ST_STRING) {
        if (b == 0x1B) {
            t->state = ST_ESC;  // ESC \ (ST) ends it; ESC-anything else restarts
        } else if ((b == 0x07 && t->state == ST_OSC) || b == 0x18 || b == 0x1A) {
            t->state = ST_GROUND;
        }
        return;
    }
    if (b < 0x20) {
        if (b == 0x1B) {
            t->state = ST_ESC;
        } else if (b == 0x18 || b == 0x1A) {  // CAN / SUB abort a sequence
            t->state = ST_GROUND;
        } else {
            control(t, b);  // C0 controls act even inside a sequence
        }
        return;
    }
    if (b == 0x7F) {
        return;  // DEL
    }

    switch (t->state) {
    case ST_GROUND:
        ground(t, b);
        break;
    case ST_ESC:
        esc_dispatch(t, b);
        break;
    case ST_ESC_OTHER:
        if (b >= 0x30) {
            t->state = ST_GROUND;
        }
        break;
    case ST_CSI:
        if (b >= '0' && b <= '9') {
            uint32_t *p = &t->params[t->nparams - 1];
            if (*p < 100000) {
                *p = *p * 10 + (b - '0');
            }
        } else if (b == ';' || b == ':') {
            t->colon |= b == ':';
            if (t->nparams < 16) {
                t->params[t->nparams++] = 0;
            }
        } else if (b >= 0x3C && b <= 0x3F) {
            t->private_marker = (char)b;
        } else if (b >= 0x20 && b <= 0x2F) {
            t->intermediate = (char)b;
        } else if (b >= 0x40 && b <= 0x7E) {
            t->state = ST_GROUND;
            csi_dispatch(t, b);
        } else {
            t->state = ST_GROUND;  // 8-bit byte inside a sequence: abort
        }
        break;
    case ST_CHARSET:
        if (t->charset_slot <= 1) {
            t->charset[t->charset_slot] = b == '0' ? '0' : 'B';
        }
        t->state = ST_GROUND;
        break;
    case ST_HASH:
        if (b == '8') {  // DECALN: fill the screen with 'E'
            t->margin_top = 0;
            t->margin_bottom = t->rows - 1;
            for (uint32_t r = 0; r < t->rows; r++) {
                term_cell_t *l = line(t, r);
                for (uint32_t c = 0; c < t->cols; c++) {
                    l[c].fg = TERM_DEFAULT_FG;
                    l[c].bg = TERM_DEFAULT_BG;
                    l[c].ch = 'E';
                    l[c].attr = 0;
                }
            }
            mark_all(t);
            move_to(t, 0, 0);
        }
        t->state = ST_GROUND;
        break;
    }
}

// --- Setup and view -----------------------------------------------------------

static int alloc_buffer(term_t *t, term_buffer_t *b, uint32_t lines) {
    size_t bytes = (size_t)lines * t->cols * sizeof(term_cell_t);
    b->cells = frame_alloc_pages((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
    if (b->cells == NULL) {
        return 0;
    }
    b->total_lines = lines;
    b->top = 0;
    b->history = 0;
    default_saved(&b->saved);
    return 1;
}

int term_init(term_t *t, uint32_t cols, uint32_t rows) {
    memset(t, 0, sizeof(*t));
    t->cols = cols < TERM_MAX_COLS ? cols : TERM_MAX_COLS;
    t->rows = rows < TERM_MAX_ROWS ? rows : TERM_MAX_ROWS;
    if (t->cols == 0 || t->rows == 0) {
        return 0;
    }
    // Scrollback is a nicety: fall back to none if memory is short.
    if (!alloc_buffer(t, &t->main_buf, t->rows + TERM_SCROLLBACK_LINES) &&
        !alloc_buffer(t, &t->main_buf, t->rows)) {
        return 0;
    }
    if (!alloc_buffer(t, &t->alt_buf, t->rows)) {
        return 0;
    }
    t->buf = &t->alt_buf;
    clear_screen(t);  // fresh pages are zero, not blanks
    full_reset(t);    // switches to and clears the main screen
    return 1;
}

void term_scroll_view(term_t *t, int delta) {
    int64_t v = (int64_t)t->view_offset + delta;
    if (v < 0) {
        v = 0;
    }
    if (v > (int64_t)t->buf->history) {
        v = t->buf->history;
    }
    if ((uint32_t)v != t->view_offset) {
        t->view_offset = (uint32_t)v;
        mark_all(t);
    }
}

const term_cell_t *term_view_cell(const term_t *t, uint32_t row, uint32_t col) {
    return buffer_line(t, t->buf, (int32_t)row - (int32_t)t->view_offset) + col;
}

bool term_cursor_shown(const term_t *t) {
    return t->cursor_visible && t->view_offset == 0;
}
