#include "video/console_fb.h"
#include "lib/string.h"
#include "video/font8x8.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8
#define TAB_WIDTH 8

// Paint one cell's glyph into the framebuffer (32 bpp, writes only).
static void draw_cell(fb_console_t *console, uint32_t col, uint32_t row) {
    // The bitmap font only covers ASCII (0-127). UTF-8 byte streams (e.g. a
    // filename with non-ASCII characters) carry bytes >= 128; render those as
    // '?' rather than indexing past the 128-glyph table.
    uint8_t glyph_index = (uint8_t)console->cells[row][col];
    if (glyph_index >= 128) {
        glyph_index = '?';
    }
    const uint8_t *glyph = font8x8_basic[glyph_index];
    uint32_t fg = console->fg_color;
    uint32_t bg = console->bg_color;

    uint8_t *line = console->fb.buffer + row * FONT_HEIGHT * console->fb.pitch +
                    col * FONT_WIDTH * sizeof(uint32_t);
    for (uint32_t y = 0; y < FONT_HEIGHT; y++) {
        uint32_t *px = (uint32_t *)line;
        uint8_t bits = glyph[y];
        for (uint32_t x = 0; x < FONT_WIDTH; x++) {
            px[x] = (bits & (1u << (7 - x))) ? fg : bg;
        }
        line += console->fb.pitch;
    }
}

static void scroll_up(fb_console_t *console) {
    memmove(console->cells[0], console->cells[1],
            (console->rows - 1) * FB_CONSOLE_MAX_COLS);
    memset(console->cells[console->rows - 1], ' ', FB_CONSOLE_MAX_COLS);
    console->dirty = true;  // redrawn on the next flush
}

static void newline(fb_console_t *console) {
    console->cursor_col = 0;
    if (console->cursor_row + 1 < console->rows) {
        console->cursor_row++;
    } else {
        scroll_up(console);
    }
}

static void put_char(fb_console_t *console, char c) {
    switch (c) {
    case '\n':
        newline(console);
        return;
    case '\r':
        console->cursor_col = 0;
        return;
    case '\b':
        if (console->cursor_col > 0) {
            console->cursor_col--;
        }
        return;
    case '\t':
        console->cursor_col = (console->cursor_col + TAB_WIDTH) & ~(uint32_t)(TAB_WIDTH - 1);
        if (console->cursor_col > console->cols) {
            console->cursor_col = console->cols;
        }
        return;
    default:
        break;
    }

    if (console->cursor_col >= console->cols) {
        newline(console);  // wrap
    }
    console->cells[console->cursor_row][console->cursor_col] = c;
    if (!console->dirty) {
        draw_cell(console, console->cursor_col, console->cursor_row);
    }
    console->cursor_col++;
}

void fb_console_flush(fb_console_t *console) {
    if (!console || !console->dirty) {
        return;
    }
    for (uint32_t row = 0; row < console->rows; row++) {
        for (uint32_t col = 0; col < console->cols; col++) {
            draw_cell(console, col, row);
        }
    }
    console->dirty = false;
}

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth) {
    if (!console || depth != 32) {
        return 0;  // draw_cell writes 32 bpp pixels
    }

    if (!framebuffer_init(&console->fb, width, height, depth)) {
        return 0;
    }

    console->cols = console->fb.width / FONT_WIDTH;
    if (console->cols > FB_CONSOLE_MAX_COLS) {
        console->cols = FB_CONSOLE_MAX_COLS;
    }
    console->rows = console->fb.height / FONT_HEIGHT;
    if (console->rows > FB_CONSOLE_MAX_ROWS) {
        console->rows = FB_CONSOLE_MAX_ROWS;
    }
    if (console->cols == 0 || console->rows == 0) {
        return 0;
    }
    console->fg_color = 0x00ffffff;
    console->bg_color = 0x00000000;

    fb_console_clear(console);
    return 1;
}

void fb_console_write(fb_console_t *console, const char *text) {
    if (!console || !text) {
        return;
    }

    while (*text) {
        put_char(console, *text++);
    }
    fb_console_flush(console);
}

void fb_console_clear(fb_console_t *console) {
    if (!console) {
        return;
    }
    memset(console->cells, ' ', sizeof(console->cells));
    console->cursor_col = 0;
    console->cursor_row = 0;
    console->dirty = false;
    framebuffer_clear(&console->fb, console->bg_color);
}

// The console the kernel and syscalls treat as "the screen".
static fb_console_t *active_console;

void fb_console_make_active(fb_console_t *console) {
    active_console = console;
}

void screen_putc(char c) {
    if (active_console) {
        put_char(active_console, c);
    }
}

void screen_flush(void) {
    fb_console_flush(active_console);
}

const framebuffer_info_t *screen_framebuffer(void) {
    return active_console ? &active_console->fb : NULL;
}
