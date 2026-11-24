// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libconsole/con_base.h"
#include "libconsole/term.h"

// Fonts for the terminal renderer (term_render.h): the built-in 8x8 font, or
// a PSF2 font (the Linux console's format) loaded from a file. A font is
// bitmaps of one size, a cell each, rows top down, each row whole bytes with
// the leftmost pixel in the top bit. Cells hold Unicode code points (term.h).
//
// The built-in font has ASCII and a few symbols (term_symbol()). A PSF2 font
// finds a code point's glyph through its Unicode table, read into a sorted
// map. For a character it lacks, it tries in turn the plain symbol that
// stands for it (a heavy or double line stands for the single one), the
// built-in symbol scaled to the cell (so lines still join), then its '?'.
//
// Pure: no allocation and no kernel or library calls, so the kernel builds it
// too (it draws with the built-in font only).

#define TERM_FONT_MAX_WIDTH 32
#define TERM_FONT_MAX_HEIGHT 64

// A code point and its glyph, in a PSF2 font's map.
typedef struct {
    uint32_t cp;
    uint32_t glyph;
} term_font_entry_t;

typedef struct term_font {
    uint32_t width, height;   // of a glyph, a cell, in pixels
    uint32_t row_bytes;       // (width + 7) / 8
    uint32_t glyph_bytes;     // row_bytes * height
    uint32_t glyph_count;
    const uint8_t *glyphs;    // NULL: the built-in font
    const term_font_entry_t *map;  // sorted by code point, no repeats
    uint32_t map_count;
    uint32_t fallback;        // the glyph of '?' (or the first)
} term_font_t;

// The built-in 8x8 font: ASCII and the TERM_GLYPH_* symbols.
extern const term_font_t term_font_builtin;

// How many entries a PSF2 font's map needs (at least 1), or -1 when it is not
// a PSF2 font term_font_psf2 can read.
long term_font_psf2_entries(const void *data, size_t size);

// Read a PSF2 font of `size` bytes at `data` into `font`, its map into `map`
// (room for `capacity` entries: term_font_psf2_entries'). Both `data` and
// `map` must stay as long as the font. 0, or -1 (the font left alone) when it
// is not a PSF2 font, is cut short, is larger than TERM_FONT_MAX_WIDTH by
// TERM_FONT_MAX_HEIGHT, or its map needs more room.
int term_font_psf2(term_font_t *font, const void *data, size_t size, term_font_entry_t *map,
                   size_t capacity);

// The bitmap for a code point: glyph_bytes bytes. NULL when the font draws
// it as the built-in symbol, scaled (term_font_pixel does both).
const uint8_t *term_font_bitmap(const term_font_t *font, uint32_t cp);

// Whether pixel (x, y) of a code point's glyph is set (x < width, y < height).
bool term_font_pixel(const term_font_t *font, uint32_t cp, uint32_t x, uint32_t y);
