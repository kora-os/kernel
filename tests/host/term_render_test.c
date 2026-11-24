// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for the console library's renderer (user/libconsole/
// term_render.c): a terminal drawn into a surface in memory, checked pixel by
// pixel against the 8x8 font and a 12x4 one, with the surface's untouched pixels holding a
// marker.

#include "test.h"
#include "libconsole/font8x8.h"
#include "libconsole/term.h"
#include "libconsole/term_render.h"

static uint8_t arena[1 << 20] __attribute__((aligned(16)));
static size_t arena_used;

static void *host_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    bytes = (bytes + 15) & ~(size_t)15;
    if (arena_used + bytes > sizeof(arena)) {
        return NULL;
    }
    void *p = arena + arena_used;
    arena_used += bytes;
    return p;
}

static const term_host_t host = {host_alloc, NULL, NULL, NULL};

#define SURFACE_W 100
#define SURFACE_H 40
#define STRIDE 104  // pixels per row: the padding past the width must stay untouched
#define MARK 0x00123456u
static uint32_t pixels[SURFACE_H * STRIDE];

static term_t t;
static term_surface_t surface;
static term_render_t render;

static void paint(uint32_t colour) {
    for (unsigned i = 0; i < SURFACE_H * STRIDE; i++) {
        pixels[i] = colour;
    }
}

// A 10 by 3 terminal drawn at (x, y) on the whole surface, with no output yet.
static void start(int32_t x, int32_t y) {
    arena_used = 0;
    CHECK(term_init(&t, 10, 3, 4, &host), "term_init");
    term_surface_init(&surface, pixels, SURFACE_W, SURFACE_H, STRIDE * 4);
    paint(MARK);
    term_render_init(&render, &t, &surface, x, y);
}

static void feed(const char *s) {
    while (*s) {
        term_putc(&t, *s++);
    }
}

static uint32_t px(int x, int y) {
    return pixels[y * STRIDE + x];
}

// Whether the cell at pixel (x, y) shows `bits` (8 rows, MSB leftmost) in fg
// on bg, for rows [from, to) of the cell.
static bool shows(int x, int y, const uint8_t bits[8], uint32_t fg, uint32_t bg, int from, int to) {
    for (int row = from; row < to; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t want = (bits[row] & (0x80 >> col)) ? fg : bg;
            if (px(x + col, y + row) != want) {
                return false;
            }
        }
    }
    return true;
}

static bool untouched(int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (px(x, y) != MARK) {
                return false;
            }
        }
    }
    return true;
}

static const uint8_t blank[8] = {0};
static const uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
#define FG TERM_DEFAULT_FG
#define BG TERM_DEFAULT_BG

static void test_glyphs_and_place(void) {
    start(5, 3);
    feed("\x1b[?25lAg");
    term_render_flush(&render);
    CHECK(shows(5, 3, font8x8_basic['A'], FG, BG, 0, 8), "'A' is drawn from the font at the origin");
    CHECK(shows(13, 3, font8x8_basic['g'], FG, BG, 0, 8), "'g' is drawn in the next cell");
    CHECK(shows(21, 3, blank, FG, BG, 0, 8) && shows(77, 19, blank, FG, BG, 0, 8),
          "the rest of the grid is blank, up to the last cell");
    CHECK(untouched(0, 0, SURFACE_W, 3) && untouched(0, 0, 5, SURFACE_H) &&
              untouched(85, 0, STRIDE, SURFACE_H) && untouched(0, 27, SURFACE_W, SURFACE_H),
          "nothing is drawn outside the grid, nor in the row padding");

    feed("\r\n\x1b(0q\x1b(B");
    term_render_flush(&render);
    CHECK(shows(5, 11, term_glyph(TERM_GLYPH_HLINE), FG, BG, 0, 8),
          "a line-drawing character takes its glyph from term_glyph()");
    feed("\x1b[31;42mX");
    term_render_flush(&render);
    CHECK(shows(13, 11, font8x8_basic['X'], 0xcd0000, 0x00cd00, 0, 8), "SGR colours are drawn");
}

static void test_attributes(void) {
    start(0, 0);
    feed("\x1b[?25l\x1b[7mA\x1b[0m\x1b[4mA\x1b[0m\x1b[9mA\x1b[0m\x1b[1mA");
    term_render_flush(&render);
    const uint8_t *a = font8x8_basic['A'];
    CHECK(shows(0, 0, a, BG, FG, 0, 8), "reverse swaps the colours");
    CHECK(shows(8, 0, a, FG, BG, 0, 7) && shows(8, 0, solid, FG, BG, 7, 8),
          "underline sets the bottom row");
    CHECK(shows(16, 0, a, FG, BG, 0, 4) && shows(16, 0, solid, FG, BG, 4, 5) &&
              shows(16, 0, a, FG, BG, 5, 8), "strike-through sets the middle row");
    uint8_t bold[8];
    for (int i = 0; i < 8; i++) {
        bold[i] = (uint8_t)(a[i] | a[i] >> 1);
    }
    CHECK(shows(24, 0, bold, FG, BG, 0, 8), "bold thickens the strokes");
}

static void test_cursor(void) {
    start(0, 0);
    feed("ab");
    term_render_flush(&render);
    CHECK(shows(16, 0, solid, FG, BG, 0, 8), "a block cursor swaps the colours of its cell");
    CHECK(shows(8, 0, font8x8_basic['b'], FG, BG, 0, 8), "the cell before it is plain");

    feed("\r\n");
    term_render_flush(&render);
    CHECK(shows(16, 0, blank, FG, BG, 0, 8), "the old cursor is erased when the cursor moves");
    CHECK(shows(0, 8, solid, FG, BG, 0, 8), "and drawn at its new place");

    feed("\x1b[4 q");
    term_render_flush(&render);
    CHECK(shows(0, 8, blank, FG, BG, 0, 6) && shows(0, 8, solid, FG, BG, 6, 8),
          "an underline cursor is its cell's two bottom rows");
    feed("\x1b[6 q");
    term_render_flush(&render);
    const uint8_t bar[8] = {0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0};
    CHECK(shows(0, 8, bar, FG, BG, 0, 8), "a bar cursor is its cell's two left columns");
    feed("\x1b[?25l");
    term_render_flush(&render);
    CHECK(shows(0, 8, blank, FG, BG, 0, 8), "a hidden cursor is not drawn");

    feed("\x1b[?25h\x1b[2 q\r\n\r\n\r\n");  // scroll: history to look at
    term_scroll_view(&t, 1);
    term_render_flush(&render);
    CHECK(shows(0, 16, blank, FG, BG, 0, 8), "the cursor is not drawn in the history view");
}

static void test_dirty_rows_only(void) {
    start(0, 0);
    feed("one\r\ntwo\r\nthree\x1b[?25l");
    term_render_flush(&render);
    paint(MARK);
    feed("\x1b[2;1HTWO");
    term_render_flush(&render);
    CHECK(untouched(0, 0, SURFACE_W, 8) && untouched(0, 16, SURFACE_W, SURFACE_H),
          "a flush redraws only the rows that changed");
    CHECK(shows(0, 8, font8x8_basic['T'], FG, BG, 0, 8), "the changed row is drawn");
    term_render_flush(&render);
    CHECK(untouched(0, 0, SURFACE_W, 8), "a flush with nothing changed draws nothing");
    term_render_all(&render);
    CHECK(shows(0, 0, font8x8_basic['o'], FG, BG, 0, 8) &&
              shows(0, 16, font8x8_basic['t'], FG, BG, 0, 8), "term_render_all redraws every row");
}

static void test_clipping(void) {
    // A clip rectangle that cuts cells in half: only its pixels are drawn.
    start(0, 0);
    surface.clip = (term_rect_t){4, 4, 50, 10};
    term_render_init(&render, &t, &surface, 0, 0);
    feed("\x1b[?25lAAAAAAAAAA\r\nAAAAAAAAAA\r\nAAAAAAAAAA");
    term_render_flush(&render);
    CHECK(untouched(0, 0, SURFACE_W, 4) && untouched(0, 0, 4, SURFACE_H) &&
              untouched(54, 0, SURFACE_W, SURFACE_H) && untouched(0, 14, SURFACE_W, SURFACE_H),
          "nothing is drawn outside the clip rectangle");
    const uint8_t *a = font8x8_basic['A'];
    bool inside = true;
    for (int y = 4; y < 14; y++) {
        for (int x = 4; x < 54; x++) {
            uint32_t want = (a[y % 8] & (0x80 >> (x % 8))) ? FG : BG;
            inside &= px(x, y) == want;
        }
    }
    CHECK(inside, "everything inside it is drawn, cut cells included");

    // A terminal partly off the surface, at negative coordinates and past the
    // right and bottom edges: the visible part is drawn, the rest skipped.
    start(-12, -5);
    feed("\x1b[?25lAAAAAAAAAA\r\nAAAAAAAAAA\r\nAAAAAAAAAA");
    term_render_flush(&render);
    bool top_rows = true;  // cell (0, 2) at (4, -5): its rows 5 to 7 are visible
    for (int row = 5; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            top_rows &= px(4 + col, row - 5) == ((a[row] & (0x80 >> col)) ? FG : BG);
        }
    }
    CHECK(shows(-12 + 16, 3, a, FG, BG, 0, 8) && top_rows,
          "the visible part of a terminal at negative coordinates is drawn");
    CHECK(px(0, 0) == ((a[5] & (0x80 >> 4)) ? FG : BG), "its first visible pixel is the right one");
    CHECK(untouched(68, 0, SURFACE_W, SURFACE_H) && untouched(0, 19, SURFACE_W, SURFACE_H),
          "nothing past its last cell");
    start(90, 35);
    feed("\x1b[?25lAAAAAAAAAA\r\nAAAAAAAAAA\r\nAAAAAAAAAA");
    term_render_flush(&render);
    CHECK(shows(90, 35, a, FG, BG, 0, 5) && untouched(0, 0, 90, SURFACE_H) &&
              untouched(0, 0, SURFACE_W, 35) && untouched(SURFACE_W, 0, STRIDE, SURFACE_H),
          "a terminal running off the right and bottom edges stops at them");
    start(SURFACE_W + 10, 0);
    feed("AAAA");
    term_render_flush(&render);
    CHECK(untouched(0, 0, STRIDE, SURFACE_H), "a terminal wholly off the surface draws nothing");
    start(0, 0);
    surface.clip = (term_rect_t){-50, -50, 2000, 2000};
    term_render_init(&render, &t, &surface, 0, 0);
    feed("AAAA");
    term_render_all(&render);
    CHECK(untouched(SURFACE_W, 0, STRIDE, SURFACE_H), "a clip larger than the surface stops at its edges");
}

// A font 12 pixels wide (two bytes a row) and 4 tall: glyph 0 is blank, and
// glyph 1, for 'A', has pixels 0-3 in row 0, 7 and 8 in row 1 (either side of
// the byte boundary) and 11 in row 2.
static const uint8_t wide_glyphs[2][8] = {
    {0},
    {0xF0, 0x00, 0x01, 0x80, 0x00, 0x10, 0x00, 0x00},
};

static bool wide_pixel(const uint8_t *glyph, int x, int y) {
    return x >= 0 && x < 12 && (glyph[y * 2 + x / 8] & (0x80 >> (x % 8))) != 0;
}

// Whether the 12x4 cell at (x, y) shows `glyph` (bold: thickened) with rows
// [solid_from, 4) solid.
static bool shows_wide(int x, int y, const uint8_t *glyph, bool bold, int solid_from) {
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 12; col++) {
            bool on = row >= solid_from || wide_pixel(glyph, col, row) ||
                      (bold && wide_pixel(glyph, col - 1, row));
            if (px(x + col, y + row) != (on ? FG : BG)) {
                return false;
            }
        }
    }
    return true;
}

static void test_font(void) {
    static const term_font_entry_t wide_map[] = {{'?', 0}, {'A', 1}};
    term_font_t wide = {.width = 12, .height = 4, .row_bytes = 2, .glyph_bytes = 8,
                        .glyph_count = 2, .glyphs = &wide_glyphs[0][0], .map = wide_map,
                        .map_count = 2};
    start(2, 1);
    CHECK(render.font == &term_font_builtin, "the built-in font by default");
    render.font = &wide;
    feed("\x1b[?25l?A\x1b[1mA\x1b[0m\x1b[4mA\r\n");
    term_render_flush(&render);
    CHECK(shows_wide(2, 1, wide_glyphs[0], false, 4), "a cell is 12x4 pixels: '?' is blank");
    CHECK(shows_wide(14, 1, wide_glyphs[1], false, 4), "'A' across two bytes a row, in the next cell");
    CHECK(shows_wide(26, 1, wide_glyphs[1], true, 4), "bold thickens across the byte boundary");
    CHECK(shows_wide(38, 1, wide_glyphs[1], false, 3), "underline: the bottom row");
    CHECK(shows_wide(2, 5, wide_glyphs[0], false, 4) && shows_wide(86, 9, wide_glyphs[0], false, 4),
          "the next row 4 pixels down, the 8th cell of the 3rd row at 86,9");
    CHECK(untouched(SURFACE_W, 0, STRIDE, SURFACE_H) && untouched(0, 13, SURFACE_W, SURFACE_H),
          "nothing below the 3 rows, nor past the surface (10 cells of 12 overflow it)");
    feed("\x1b[?25h\x1b[4 q");
    term_render_flush(&render);
    CHECK(shows_wide(2, 5, wide_glyphs[0], false, 2), "an underline cursor: the bottom two rows");
}

TEST_MAIN(test_glyphs_and_place, test_attributes, test_cursor, test_dirty_rows_only, test_clipping,
          test_font)
