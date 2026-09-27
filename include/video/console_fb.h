#pragma once

#include "common.h"
#include "video/framebuffer.h"
#include "video/term.h"

// Text console on a 32 bpp framebuffer: renders a terminal (video/term.h) with
// the 8x8 font. The terminal marks the rows that changed; flushing redraws just
// those (writes only -- the framebuffer is uncached, so it is never read back)
// and then the cursor. A burst of output that scrolls costs one full redraw at
// the next flush, not one per line.

typedef struct {
    framebuffer_info_t fb;
    term_t term;
    bool cursor_drawn;    // the cursor is on screen, in row cursor_row
    uint32_t cursor_row;
} fb_console_t;

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth);

// Write plain text ('\n' starts a new line) and flush.
void fb_console_write(fb_console_t *console, const char *text);

// Bring the screen up to date with the terminal.
void fb_console_flush(fb_console_t *console);

// Designate a console as the active screen, so the screen_* calls and the
// fb_info syscall target it.
void fb_console_make_active(fb_console_t *console);

// Feed one byte to the active screen's terminal (escape sequences included; no
// newline translation). No-op if no screen is active. Call screen_flush() when a
// batch of output is done.
void screen_putc(char c);
void screen_flush(void);

// Scroll the active screen's history view by `halfpages` half screens
// (positive = back in time). The view returns to the live screen on output.
void screen_scroll_view(int halfpages);

// The active screen's framebuffer, or NULL if no console is active. Used by the
// fb_info syscall to hand user programs the framebuffer geometry and address.
const framebuffer_info_t *screen_framebuffer(void);
