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

// Designate a console as the active screen, so screen_putc()/screen_framebuffer()
// (used by the printf backend and the write/fb_info syscalls) target it.
void fb_console_make_active(fb_console_t *console);

// Draw one character to the active screen console; no-op if none is active.
void screen_putc(char c);

// The active screen's framebuffer, or NULL if no console is active. Used by the
// fb_info syscall to hand user programs the framebuffer geometry and address.
const framebuffer_info_t *screen_framebuffer(void);
