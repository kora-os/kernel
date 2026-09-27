#pragma once

#include "common.h"
#include "video/framebuffer.h"

// Text console on a 32 bpp framebuffer. The console keeps its own grid of
// character cells and draws the screen from it, so scrolling shifts the grid
// and redraws (framebuffer writes only) instead of reading pixels back from the
// uncached framebuffer, which is slow. A scroll only marks the screen dirty;
// the redraw happens once, on the next flush, so a burst of output costs one
// redraw rather than one per line.

#define FB_CONSOLE_MAX_COLS 240  // 1920 / 8
#define FB_CONSOLE_MAX_ROWS 135  // 1080 / 8

typedef struct {
    framebuffer_info_t fb;
    uint32_t cols;        // grid size in cells, fitted to the framebuffer
    uint32_t rows;
    uint32_t cursor_col;  // may equal cols: the wrap happens on the next char
    uint32_t cursor_row;
    uint32_t fg_color;
    uint32_t bg_color;
    bool dirty;           // grid scrolled since the screen was last redrawn
    char cells[FB_CONSOLE_MAX_ROWS][FB_CONSOLE_MAX_COLS];
} fb_console_t;

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth);
void fb_console_write(fb_console_t *console, const char *text);
void fb_console_clear(fb_console_t *console);

// Bring the screen up to date with the grid (a full redraw if it scrolled).
void fb_console_flush(fb_console_t *console);

// Designate a console as the active screen, so screen_putc()/screen_framebuffer()
// (used by the tty and the fb_info syscall) target it.
void fb_console_make_active(fb_console_t *console);

// Write one character to the active screen console; no-op if none is active.
// Call screen_flush() when a batch of output is done.
void screen_putc(char c);
void screen_flush(void);

// The active screen's framebuffer, or NULL if no console is active. Used by the
// fb_info syscall to hand user programs the framebuffer geometry and address.
const framebuffer_info_t *screen_framebuffer(void);
