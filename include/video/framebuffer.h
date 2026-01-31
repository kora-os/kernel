#pragma once

#include "common.h"

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t depth;
    uint8_t *buffer;
} framebuffer_info_t;

int framebuffer_init(framebuffer_info_t *fb, uint32_t width, uint32_t height, uint32_t depth);
void framebuffer_clear(framebuffer_info_t *fb, uint32_t color);
void framebuffer_put_pixel(framebuffer_info_t *fb, uint32_t x, uint32_t y, uint32_t color);

