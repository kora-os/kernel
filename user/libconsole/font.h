// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libconsole/con_base.h"
#include "libconsole/term.h"

// Fonts for the terminal renderer (term_render.h): the built-in 8x8 font, or
// a PSF2 font (the Linux console's format) loaded from a file. A font is
// bitmaps of one size, a cell each, rows top down, each row whole bytes with
// the leftmost pixel in the top bit. Cells hold ASCII or a TERM_GLYPH_* code
// (term.h); a PSF2 font finds the glyph for each through its Unicode table.
// A symbol (TERM_GLYPH_*) the font lacks is the built-in one, scaled to the
// cell, so lines still join; other characters it lacks show its '?'.
//
// Pure: no allocation and no kernel or library calls, so the kernel builds it
// too (it draws with the built-in font only).

#define TERM_FONT_MAX_WIDTH 32
#define TERM_FONT_MAX_HEIGHT 64

typedef struct term_font {
    uint32_t width, height;   // of a glyph, a cell, in pixels
    uint32_t row_bytes;       // (width + 7) / 8
    uint32_t glyph_bytes;     // row_bytes * height
    uint32_t glyph_count;
    const uint8_t *glyphs;    // NULL: the built-in font
    // The glyph each cell code shows (a PSF2 font's), or TERM_FONT_SCALED.
    uint16_t code_glyph[TERM_GLYPH_END];
} term_font_t;

#define TERM_FONT_SCALED 0xFFFF  // the built-in 8x8 glyph, scaled

// The built-in 8x8 font: ASCII and the TERM_GLYPH_* symbols.
extern const term_font_t term_font_builtin;

// Read a PSF2 font of `size` bytes at `data`, which must stay as long as the
// font: its glyphs are read from there. ASCII the font has no glyph for shows
// its '?' (or its first glyph). 0, or -1 when it is not a PSF2 font, is cut
// short, or is larger than TERM_FONT_MAX_WIDTH by TERM_FONT_MAX_HEIGHT.
int term_font_psf2(term_font_t *font, const void *data, size_t size);

// The bitmap for a cell code: glyph_bytes bytes. NULL for a symbol the font
// draws scaled from the built-in one (term_font_pixel does both).
const uint8_t *term_font_bitmap(const term_font_t *font, uint8_t code);

// Whether pixel (x, y) of a cell code's glyph is set (x < width, y < height).
bool term_font_pixel(const term_font_t *font, uint8_t code, uint32_t x, uint32_t y);
