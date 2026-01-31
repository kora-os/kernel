#pragma once

#include "common.h"
#include "video/framebuffer.h"

typedef struct {
    framebuffer_info_t fb;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t fg_color;
    uint32_t bg_color;
} fb_console_t;

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth);
void fb_console_write(fb_console_t *console, const char *text);
void fb_console_clear(fb_console_t *console);
