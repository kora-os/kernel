// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libconsole/con_base.h"
#include "libconsole/font.h"
#include "libconsole/term.h"

// Draws a terminal (libconsole/term.h) into a surface with a font
// (libconsole/font.h): the built-in 8x8 one unless the program sets another.
// The terminal marks the rows that changed; a flush redraws just those, then
// the cursor. It only writes pixels, never reads them, so a surface may be
// uncached memory such as a framebuffer.

// The size of a cell of the built-in font, in pixels.
#define TERM_CELL_WIDTH 8
#define TERM_CELL_HEIGHT 8

typedef struct {
    int32_t x, y;
    uint32_t width, height;
} term_rect_t;

// Pixels to draw on: a framebuffer, part of one, or a buffer in memory. A
// pixel is 32 bits, 0x00RRGGBB; `pitch` is the number of bytes from one row of
// pixels to the next. Nothing is drawn outside `clip`, nor outside the
// surface itself.
typedef struct {
    uint8_t *pixels;
    uint32_t width, height;
    uint32_t pitch;
    term_rect_t clip;
} term_surface_t;

// A surface of `width` by `height` pixels at `pixels`, clipped to all of it.
void term_surface_init(term_surface_t *surface, void *pixels, uint32_t width,
                       uint32_t height, uint32_t pitch);

typedef struct {
    term_t *term;
    term_surface_t surface;
    int32_t x, y;          // where the top-left cell goes on the surface
    const term_font_t *font;  // a cell is font->width by font->height pixels
    bool cursor_drawn;     // the cursor is drawn, in row cursor_row
    uint32_t cursor_row;
} term_render_t;

// Draw `term` on a copy of `surface` with its top-left cell at (x, y), which
// may lie partly or wholly outside the surface, with the built-in font. Draws
// nothing yet. A program may change `surface`, `x`, `y` and `font` later (the
// font as large as the terminal's cells were sized for), then call
// term_render_all().
void term_render_init(term_render_t *render, term_t *term, const term_surface_t *surface,
                      int32_t x, int32_t y);

// Bring the pixels up to date with the terminal: the rows it marked dirty,
// the row the cursor was drawn in, then the cursor.
void term_render_flush(term_render_t *render);

// Redraw every row, as after the pixels were painted over.
void term_render_all(term_render_t *render);
