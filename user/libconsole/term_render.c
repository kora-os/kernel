// SPDX-License-Identifier: GPL-3.0-or-later
//
// Terminal renderer for 32-bit surfaces. See term_render.h.

#include "libconsole/term_render.h"

#include "libconsole/font8x8.h"

#define NO_CURSOR 0xFF

// The part of the surface that may be drawn on: its clip rectangle within its
// bounds, as half-open pixel ranges.
typedef struct {
    int64_t x0, x1, y0, y1;
} bounds_t;

static int64_t max64(int64_t a, int64_t b) {
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b) {
    return a < b ? a : b;
}

static bounds_t drawable(const term_surface_t *s) {
    bounds_t b = {
        max64(0, s->clip.x),
        min64(s->width, (int64_t)s->clip.x + s->clip.width),
        max64(0, s->clip.y),
        min64(s->height, (int64_t)s->clip.y + s->clip.height),
    };
    return b;
}

// Paint one cell, optionally with the cursor on it, as far as it is inside
// `b`.
static void draw_cell(const term_render_t *r, const bounds_t *b, uint32_t row, uint32_t col,
                      const term_cell_t *cell, uint8_t cursor) {
    int64_t left = (int64_t)r->x + (int64_t)col * TERM_CELL_WIDTH;
    int64_t top = (int64_t)r->y + (int64_t)row * TERM_CELL_HEIGHT;
    int64_t x0 = max64(left, b->x0), x1 = min64(left + TERM_CELL_WIDTH, b->x1);
    int64_t y0 = max64(top, b->y0), y1 = min64(top + TERM_CELL_HEIGHT, b->y1);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    uint32_t fg = cell->fg;
    uint32_t bg = cell->bg;
    if (cell->attr & TERM_ATTR_REVERSE) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    if (cursor == TERM_CURSOR_BLOCK) {
        uint32_t tmp = fg;
        fg = bg;
        bg = tmp;
    }
    const uint8_t *glyph = cell->ch < 0x80 ? font8x8_basic[cell->ch] : term_glyph(cell->ch);

    for (int64_t py = y0; py < y1; py++) {
        uint32_t y = (uint32_t)(py - top);
        uint8_t bits = glyph[y];
        if (cell->attr & TERM_ATTR_BOLD) {
            bits |= bits >> 1;  // no bold font: thicken the strokes
        }
        if ((cell->attr & TERM_ATTR_UNDERLINE) && y == TERM_CELL_HEIGHT - 1) {
            bits = 0xFF;
        }
        if ((cell->attr & TERM_ATTR_STRIKE) && y == TERM_CELL_HEIGHT / 2) {
            bits = 0xFF;
        }
        if (cursor == TERM_CURSOR_UNDERLINE && y >= TERM_CELL_HEIGHT - 2) {
            bits = 0xFF;
        }
        if (cursor == TERM_CURSOR_BAR) {
            bits |= 0xC0;
        }
        uint32_t *px = (uint32_t *)(r->surface.pixels + (size_t)py * r->surface.pitch);
        for (int64_t pxx = x0; pxx < x1; pxx++) {
            uint32_t x = (uint32_t)(pxx - left);
            px[pxx] = (bits & (1u << (7 - x))) ? fg : bg;
        }
    }
}

void term_surface_init(term_surface_t *surface, void *pixels, uint32_t width,
                       uint32_t height, uint32_t pitch) {
    surface->pixels = (uint8_t *)pixels;
    surface->width = width;
    surface->height = height;
    surface->pitch = pitch;
    surface->clip.x = 0;
    surface->clip.y = 0;
    surface->clip.width = width;
    surface->clip.height = height;
}

void term_render_init(term_render_t *render, term_t *term, const term_surface_t *surface,
                      int32_t x, int32_t y) {
    render->term = term;
    render->surface = *surface;
    render->x = x;
    render->y = y;
    render->cursor_drawn = false;
    render->cursor_row = 0;
}

void term_render_flush(term_render_t *render) {
    term_t *t = render->term;
    bounds_t b = drawable(&render->surface);

    // Redraw the row the cursor was drawn in, which erases it.
    if (render->cursor_drawn) {
        t->dirty[render->cursor_row] = true;
    }
    for (uint32_t row = 0; row < t->rows; row++) {
        if (!t->dirty[row]) {
            continue;
        }
        for (uint32_t col = 0; col < t->cols; col++) {
            draw_cell(render, &b, row, col, term_view_cell(t, row, col), NO_CURSOR);
        }
        t->dirty[row] = false;
    }

    render->cursor_drawn = false;
    if (term_cursor_shown(t)) {
        draw_cell(render, &b, t->row, t->col, term_view_cell(t, t->row, t->col),
                  t->cursor_style);
        render->cursor_drawn = true;
        render->cursor_row = t->row;
    }
}

void term_render_all(term_render_t *render) {
    term_t *t = render->term;
    for (uint32_t row = 0; row < t->rows; row++) {
        t->dirty[row] = true;
    }
    term_render_flush(render);
}
