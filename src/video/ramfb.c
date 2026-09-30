// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/ramfb.h"

#if defined(KORAOS_VIRT) && defined(KORAOS_RAMFB)
#include "drivers/fw_cfg.h"
#include "mm/coherent.h"
#include "mm/mmu.h"
#include "platform/virt.h"

#define RAMFB_CAPACITY (4U * 1024U * 1024U)
// Limits used by QEMU ramfb (bochs-vbe.h).
#define RAMFB_MAX_WIDTH 16000U
#define RAMFB_MAX_HEIGHT 12000U
#define RAMFB_BLOCK_SIZE (2U * 1024U * 1024U)
#define COHERENT_SLOT_RAMFB_CONFIG 126
// DRM_FORMAT_XRGB8888: little-endian pixel bytes B,G,R,X match 0x00RRGGBB.
#define DRM_FORMAT_XRGB8888 0x34325258U

struct ramfb_config {
    uint64_t address;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
} __attribute__((packed));

_Static_assert(sizeof(struct ramfb_config) == 28, "ramfb configuration size");

// Own whole MMU blocks so changing cacheability cannot affect unrelated data.
// BSS storage also keeps this reservation out of the general-purpose allocator.
static uint8_t pixels[RAMFB_CAPACITY] __attribute__((aligned(RAMFB_BLOCK_SIZE)));

int ramfb_init(framebuffer_info_t *fb, uint32_t width, uint32_t height, uint32_t depth) {
    if (!fb) {
        return 0;
    }
    *fb = (framebuffer_info_t){0};
    // QEMU's display accepts dimensions >=16. Validate before multiplication.
    if (depth != 32 || width < 16 || height < 16 ||
        width > RAMFB_MAX_WIDTH || height > RAMFB_MAX_HEIGHT ||
        width > RAMFB_CAPACITY / 4 || height > RAMFB_CAPACITY / (width * 4)) {
        return 0;
    }
    uint32_t pitch = width * 4;
    const struct virt_platform *platform = virt_platform_get();
    if (!platform || !fw_cfg_init(platform->fw_cfg_base)) {
        return 0;
    }
    uint16_t selector;
    uint32_t config_size;
    if (!fw_cfg_find_file("etc/ramfb", &selector, &config_size) ||
        config_size != sizeof(struct ramfb_config)) {
        return 0;
    }
    struct ramfb_config *config = coherent_page(COHERENT_SLOT_RAMFB_CONFIG);
    if (!config) {
        return 0;
    }
    mmu_map_coherent((uintptr_t)pixels, sizeof(pixels));
    config->address = __builtin_bswap64((uintptr_t)pixels);
    config->fourcc = __builtin_bswap32(DRM_FORMAT_XRGB8888);
    config->flags = 0;
    config->width = __builtin_bswap32(width);
    config->height = __builtin_bswap32(height);
    config->stride = __builtin_bswap32(pitch);
    if (!fw_cfg_write(selector, config, sizeof(*config))) {
        return 0;
    }
    fb->width = width;
    fb->height = height;
    fb->depth = depth;
    fb->pitch = pitch;
    fb->buffer = pixels;
    return 1;
}
#endif
