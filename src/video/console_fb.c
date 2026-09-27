#include "video/console_fb.h"
#include "video/font8x8.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define NO_CURSOR 0xFF

// Paint one cell into the framebuffer (32 bpp, writes only), optionally with the
// cursor on it.
static void draw_cell(fb_console_t *console, uint32_t row, uint32_t col,
                      const term_cell_t *cell, uint8_t cursor) {
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

    uint8_t *line = console->fb.buffer + row * FONT_HEIGHT * console->fb.pitch +
                    col * FONT_WIDTH * sizeof(uint32_t);
    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint8_t bits = glyph[y];
        if (cell->attr & TERM_ATTR_BOLD) {
            bits |= bits >> 1;  // no bold font: thicken the strokes
        }
        if ((cell->attr & TERM_ATTR_UNDERLINE) && y == FONT_HEIGHT - 1) {
            bits = 0xFF;
        }
        if ((cell->attr & TERM_ATTR_STRIKE) && y == FONT_HEIGHT / 2) {
            bits = 0xFF;
        }
        if (cursor == TERM_CURSOR_UNDERLINE && y >= FONT_HEIGHT - 2) {
            bits = 0xFF;
        }
        if (cursor == TERM_CURSOR_BAR) {
            bits |= 0xC0;
        }
        uint32_t *px = (uint32_t *)line;
        for (uint32_t x = 0; x < FONT_WIDTH; x++) {
            px[x] = (bits & (1u << (7 - x))) ? fg : bg;
        }
        line += console->fb.pitch;
    }
}

void fb_console_flush(fb_console_t *console) {
    if (!console) {
        return;
    }
    term_t *t = &console->term;

    // Redraw the row the cursor was drawn in, which erases it.
    if (console->cursor_drawn) {
        t->dirty[console->cursor_row] = true;
    }
    for (uint32_t row = 0; row < t->rows; row++) {
        if (!t->dirty[row]) {
            continue;
        }
        for (uint32_t col = 0; col < t->cols; col++) {
            draw_cell(console, row, col, term_view_cell(t, row, col), NO_CURSOR);
        }
        t->dirty[row] = false;
    }

    console->cursor_drawn = false;
    if (term_cursor_shown(t)) {
        draw_cell(console, t->row, t->col, term_view_cell(t, t->row, t->col),
                  t->cursor_style);
        console->cursor_drawn = true;
        console->cursor_row = t->row;
    }
}

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth) {
    if (!console || depth != 32) {
        return 0;  // draw_cell writes 32 bpp pixels
    }
    if (!framebuffer_init(&console->fb, width, height, depth)) {
        return 0;
    }
    if (!term_init(&console->term, console->fb.width / FONT_WIDTH,
                   console->fb.height / FONT_HEIGHT)) {
        return 0;
    }
    console->cursor_drawn = false;
    framebuffer_clear(&console->fb, TERM_DEFAULT_BG);  // incl. any partial cell
    fb_console_flush(console);
    return 1;
}

void fb_console_write(fb_console_t *console, const char *text) {
    if (!console || !text) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            term_putc(&console->term, '\r');
        }
        term_putc(&console->term, *text++);
    }
    fb_console_flush(console);
}

// The console the kernel and syscalls treat as "the screen".
static fb_console_t *active_console;

void fb_console_make_active(fb_console_t *console) {
    active_console = console;
}

void screen_putc(char c) {
    if (active_console) {
        term_putc(&active_console->term, c);
    }
}

void screen_flush(void) {
    fb_console_flush(active_console);
}

void screen_scroll_view(int halfpages) {
    if (active_console) {
        term_t *t = &active_console->term;
        term_scroll_view(t, halfpages * (int)(t->rows / 2));
    }
}

const framebuffer_info_t *screen_framebuffer(void) {
    return active_console ? &active_console->fb : NULL;
}
