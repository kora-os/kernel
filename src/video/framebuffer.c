#include "video/framebuffer.h"
#include "drivers/mailbox.h"

#define TAG_SET_PHYS_WH 0x00048003
#define TAG_SET_VIRT_WH 0x00048004
#define TAG_SET_DEPTH 0x00048005
#define TAG_SET_PIXEL_ORDER 0x00048006
#define TAG_ALLOCATE_BUFFER 0x00040001
#define TAG_GET_PITCH 0x00040008
#define TAG_END 0x00000000

#define PIXEL_ORDER_RGB 0
#define PIXEL_ORDER_BGR 1

static volatile uint32_t framebuffer_mailbox[256] __attribute__((aligned(16)));

static uint32_t bus_to_phys(uint32_t addr) {
#ifdef QEMU_TESTING
    return addr;
#else
    return addr & 0x3FFFFFFF;
#endif
}

int framebuffer_init(framebuffer_info_t *fb, uint32_t width, uint32_t height, uint32_t depth) {
    if (!fb) {
        return 0;
    }

    int idx = 0;
    framebuffer_mailbox[idx++] = 0; // total size filled later
    framebuffer_mailbox[idx++] = 0; // request

    framebuffer_mailbox[idx++] = TAG_SET_PHYS_WH;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = width;
    framebuffer_mailbox[idx++] = height;

    framebuffer_mailbox[idx++] = TAG_SET_VIRT_WH;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = width;
    framebuffer_mailbox[idx++] = height;

    framebuffer_mailbox[idx++] = TAG_SET_DEPTH;
    framebuffer_mailbox[idx++] = 4;
    framebuffer_mailbox[idx++] = 4;
    framebuffer_mailbox[idx++] = depth;

    framebuffer_mailbox[idx++] = TAG_SET_PIXEL_ORDER;
    framebuffer_mailbox[idx++] = 4;
    framebuffer_mailbox[idx++] = 4;
    framebuffer_mailbox[idx++] = PIXEL_ORDER_RGB;

    int alloc_tag_index = idx;
    framebuffer_mailbox[idx++] = TAG_ALLOCATE_BUFFER;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = 8;
    framebuffer_mailbox[idx++] = 16;
    framebuffer_mailbox[idx++] = 0;

    int pitch_tag_index = idx;
    framebuffer_mailbox[idx++] = TAG_GET_PITCH;
    framebuffer_mailbox[idx++] = 4;
    framebuffer_mailbox[idx++] = 0;
    framebuffer_mailbox[idx++] = 0;

    framebuffer_mailbox[idx++] = TAG_END;

    framebuffer_mailbox[0] = idx * 4;

    if (!mailbox_call(MAILBOX_CHANNEL_PROPERTY, framebuffer_mailbox)) {
        return 0;
    }

    uint32_t fb_addr = framebuffer_mailbox[alloc_tag_index + 3];
    uint32_t fb_size = framebuffer_mailbox[alloc_tag_index + 4];
    uint32_t pitch = framebuffer_mailbox[pitch_tag_index + 3];

    if (fb_addr == 0 || fb_size == 0 || pitch == 0) {
        return 0;
    }

    fb->width = width;
    fb->height = height;
    fb->depth = depth;
    fb->pitch = pitch;
    fb->buffer = (uint8_t *)(uintptr_t)bus_to_phys(fb_addr);

    return 1;
}

void framebuffer_put_pixel(framebuffer_info_t *fb, uint32_t x, uint32_t y, uint32_t color) {
    if (!fb || !fb->buffer) {
        return;
    }
    if (x >= fb->width || y >= fb->height) {
        return;
    }

    uint32_t bytes_per_pixel = fb->depth / 8;
    uint32_t offset = y * fb->pitch + x * bytes_per_pixel;
    *(uint32_t *)(fb->buffer + offset) = color;
}

void framebuffer_clear(framebuffer_info_t *fb, uint32_t color) {
    if (!fb || !fb->buffer) {
        return;
    }

    uint32_t bytes_per_pixel = fb->depth / 8;
    for (uint32_t y = 0; y < fb->height; y++) {
        uint8_t *row = fb->buffer + y * fb->pitch;
        for (uint32_t x = 0; x < fb->width; x++) {
            *(uint32_t *)(row + x * bytes_per_pixel) = color;
        }
    }
}
