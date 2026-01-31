#include "video/console_fb.h"
#include "video/font8x8.h"

#define FONT_WIDTH 8
#define FONT_HEIGHT 8

static void fb_console_newline(fb_console_t *console) {
    console->cursor_x = 0;
    console->cursor_y += FONT_HEIGHT;
}

static void fb_console_draw_char(fb_console_t *console, char c) {
    if (!console || !console->fb.buffer) {
        return;
    }

    if (c == '\n') {
        fb_console_newline(console);
        return;
    }
    if (c == '\r') {
        console->cursor_x = 0;
        return;
    }

    uint32_t x0 = console->cursor_x;
    uint32_t y0 = console->cursor_y;

    if (x0 + FONT_WIDTH > console->fb.width) {
        fb_console_newline(console);
        x0 = console->cursor_x;
        y0 = console->cursor_y;
    }
    if (y0 + FONT_HEIGHT > console->fb.height) {
        console->cursor_x = 0;
        console->cursor_y = 0;
        framebuffer_clear(&console->fb, console->bg_color);
        x0 = 0;
        y0 = 0;
    }

    const uint8_t *glyph = font8x8_basic[(uint8_t)c];
    for (uint32_t row = 0; row < FONT_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (uint32_t col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (bits & (1u << (7 - col))) ? console->fg_color : console->bg_color;
            framebuffer_put_pixel(&console->fb, x0 + col, y0 + row, color);
        }
    }

    console->cursor_x += FONT_WIDTH;
}

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth) {
    if (!console) {
        return 0;
    }

    if (!framebuffer_init(&console->fb, width, height, depth)) {
        return 0;
    }

    console->cursor_x = 0;
    console->cursor_y = 0;
    console->fg_color = 0x00ffffff;
    console->bg_color = 0x00000000;

    framebuffer_clear(&console->fb, console->bg_color);
    return 1;
}

void fb_console_write(fb_console_t *console, const char *text) {
    if (!console || !text) {
        return;
    }

    while (*text) {
        fb_console_draw_char(console, *text++);
    }
}

void fb_console_clear(fb_console_t *console) {
    if (!console) {
        return;
    }
    console->cursor_x = 0;
    console->cursor_y = 0;
    framebuffer_clear(&console->fb, console->bg_color);
}
