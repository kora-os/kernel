// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// Terminal model: a grid of character cells driven by an xterm-compatible
// escape-sequence parser (the subset the xterm-256color terminfo entry uses:
// cursor movement and addressing, erase/insert/delete, scroll margins, SGR with
// 16/256/true colour, alternate screen, save/restore cursor, tab stops, the DEC
// line-drawing charset, status reports) plus scrollback. It knows nothing about
// pixels: a renderer (video/console_fb.c) draws the rows it marks dirty.

// Cursor shapes (DECSCUSR can change it at runtime; this is the default and what
// "CSI 0 SP q" resets to).
#define TERM_CURSOR_BLOCK 0
#define TERM_CURSOR_UNDERLINE 1
#define TERM_CURSOR_BAR 2
#ifndef TERM_DEFAULT_CURSOR
#define TERM_DEFAULT_CURSOR TERM_CURSOR_BLOCK
#endif

// Lines of history kept above the screen (main screen only).
#ifndef TERM_SCROLLBACK_LINES
#define TERM_SCROLLBACK_LINES 1000
#endif

#define TERM_MAX_COLS 240  // 1920 / 8
#define TERM_MAX_ROWS 135  // 1080 / 8

// Default colours (0x00RRGGBB).
#define TERM_DEFAULT_FG 0x00ffffff
#define TERM_DEFAULT_BG 0x00000000

// Cell attributes the renderer draws.
#define TERM_ATTR_BOLD 0x01
#define TERM_ATTR_UNDERLINE 0x02
#define TERM_ATTR_REVERSE 0x04
#define TERM_ATTR_STRIKE 0x08

// Glyph codes >= 0x80 are line-drawing and symbol glyphs (see term_glyph()).
enum {
    TERM_GLYPH_HLINE = 0x80,
    TERM_GLYPH_VLINE,
    TERM_GLYPH_ULCORNER,
    TERM_GLYPH_URCORNER,
    TERM_GLYPH_LLCORNER,
    TERM_GLYPH_LRCORNER,
    TERM_GLYPH_LTEE,
    TERM_GLYPH_RTEE,
    TERM_GLYPH_TTEE,
    TERM_GLYPH_BTEE,
    TERM_GLYPH_CROSS,
    TERM_GLYPH_CHECKER,
    TERM_GLYPH_BLOCK,
    TERM_GLYPH_DIAMOND,
    TERM_GLYPH_BULLET,
    TERM_GLYPH_DEGREE,
    TERM_GLYPH_PLUSMINUS,
    TERM_GLYPH_END
};

typedef struct {
    uint32_t fg;   // resolved 0x00RRGGBB
    uint32_t bg;
    uint8_t ch;    // ASCII, or a TERM_GLYPH_* code
    uint8_t attr;  // TERM_ATTR_*
} term_cell_t;

// A pen colour: the terminal default, a palette index (0..255), or 24-bit RGB.
typedef struct {
    uint8_t kind;  // TERM_COLOR_*
    uint32_t value;
} term_color_t;

#define TERM_COLOR_DEFAULT 0
#define TERM_COLOR_INDEX 1
#define TERM_COLOR_RGB 2

typedef struct {
    term_color_t fg;
    term_color_t bg;
    uint8_t attr;     // TERM_ATTR_* plus the unrendered ones below
} term_pen_t;

// State saved by DECSC / CSI s and restored by DECRC / CSI u.
typedef struct {
    uint32_t col, row;
    bool wrap_pending;
    bool origin_mode;
    term_pen_t pen;
    uint8_t charset[2];
    uint8_t gl;
} term_saved_cursor_t;

// One screen's worth of lines plus (for the main screen) history, as a ring:
// screen row 0 is ring line `top`, history lines sit just before it.
typedef struct {
    term_cell_t *cells;     // total_lines * cols
    uint32_t total_lines;   // rows + history capacity
    uint32_t top;           // ring index of screen row 0
    uint32_t history;       // history lines currently held
    term_saved_cursor_t saved;
} term_buffer_t;

typedef struct {
    uint32_t cols, rows;
    term_buffer_t main_buf, alt_buf;
    term_buffer_t *buf;     // the screen being shown: &main_buf or &alt_buf

    // Cursor. wrap_pending: a glyph was written in the last column and the
    // next one wraps first (xterm's deferred autowrap).
    uint32_t col, row;
    bool wrap_pending;
    bool cursor_visible;
    uint8_t cursor_style;   // TERM_CURSOR_*

    term_pen_t pen;
    uint8_t charset[2];     // G0/G1: 'B' ASCII or '0' DEC special graphics
    uint8_t gl;             // which of G0/G1 is active (SI/SO)
    uint8_t last_glyph;     // for REP

    // Modes.
    bool autowrap;          // DECAWM
    bool origin_mode;       // DECOM
    bool insert_mode;       // IRM
    bool newline_mode;      // LNM
    uint32_t margin_top, margin_bottom;  // DECSTBM, inclusive
    bool tab_stops[TERM_MAX_COLS];

    // Scrollback view: how many lines back from the live screen is shown.
    uint32_t view_offset;

    // Rows the renderer must redraw.
    bool dirty[TERM_MAX_ROWS];

    // Parser.
    uint8_t state;
    uint32_t params[16];
    uint32_t nparams;
    char private_marker;    // '?', '>', '=' or '<' after CSI, or 0
    char intermediate;      // e.g. ' ' in DECSCUSR, '!' in DECSTR
    bool colon;             // ':' separators seen (SGR 38:2::r:g:b form)
    uint8_t charset_slot;   // which G-set an ESC ( / ESC ) designates
    uint32_t utf8_cp;
    uint8_t utf8_left;
} term_t;

// Size the grid and allocate its buffers (from the frame allocator). Returns 0
// if memory is short.
int term_init(term_t *t, uint32_t cols, uint32_t rows);

// Feed one byte of output.
void term_putc(term_t *t, char c);

// Move the scrollback view by `delta` lines (positive = further back). Clamped.
void term_scroll_view(term_t *t, int delta);

// The cell shown at screen position (row, col), taking the scrollback view into
// account.
const term_cell_t *term_view_cell(const term_t *t, uint32_t row, uint32_t col);

// Whether the cursor should be drawn (visible and the view is live).
bool term_cursor_shown(const term_t *t);

// 8x8 bitmap (MSB = leftmost pixel) for a TERM_GLYPH_* code.
const uint8_t *term_glyph(uint8_t code);
