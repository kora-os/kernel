// SPDX-License-Identifier: GPL-3.0-or-later
#include "test.h"
#include "drivers/fw_cfg.h"
#include "platform/virt.h"
#include "video/ramfb.h"

static struct virt_platform platform;
static uint8_t config_page[4096] __attribute__((aligned(4096)));
static int init_ok, file_ok, write_ok, page_ok;
static uint32_t file_size;
static unsigned init_calls, find_calls, write_calls, map_calls;
static uintptr_t mapped_base;
static size_t mapped_size;
static uint8_t wire[28];

const struct virt_platform *virt_platform_get(void) {
    return &platform;
}

int fw_cfg_init(uintptr_t base) {
    init_calls++;
    CHECK(base == platform.fw_cfg_base, "use discovered fw_cfg base");
    return base && init_ok;
}

int fw_cfg_find_file(const char *name, uint16_t *selector, uint32_t *size) {
    find_calls++;
    CHECK(!strcmp(name, "etc/ramfb"), "look up exact ramfb file name");
    *selector = 0x2A;
    *size = file_size;
    return file_ok;
}

void *coherent_page(unsigned slot) {
    CHECK(slot == 126, "ramfb config uses private coherent slot");
    return page_ok ? config_page : NULL;
}

void mmu_map_coherent(uintptr_t base, size_t size) {
    map_calls++;
    mapped_base = base;
    mapped_size = size;
}

int fw_cfg_write(uint16_t selector, const void *buffer, uint32_t size) {
    write_calls++;
    CHECK(selector == 0x2A && size == sizeof(wire), "write exact directory selector and size");
    CHECK(buffer == config_page, "DMA config lives in coherent RAM");
    CHECK(map_calls == 1, "map scanout before notifying device");
    for (unsigned i = 0; i < sizeof(wire); i++) {
        wire[i] = ((const uint8_t *)buffer)[i];
    }
    return write_ok;
}

static uint32_t be32(unsigned offset) {
    return (uint32_t)wire[offset] << 24 | (uint32_t)wire[offset + 1] << 16 |
           (uint32_t)wire[offset + 2] << 8 | wire[offset + 3];
}

static void reset(void) {
    platform.fw_cfg_base = 0x9020000;
    init_ok = file_ok = write_ok = page_ok = 1;
    file_size = 28;
    init_calls = find_calls = write_calls = map_calls = 0;
    mapped_base = mapped_size = 0;
}

static void test_invalid_modes(void) {
    const uint32_t modes[][3] = {
        {0, 768, 32}, {1024, 0, 32}, {15, 768, 32}, {1024, 15, 32},
        {1024, 768, 16}, {1024, 768, 24}, {1024, 768, 0},
        {0xFFFFFFFFU, 16, 32}, {16, 0xFFFFFFFFU, 32},
        {16001, 16, 32}, {16, 12001, 32}, {1024, 1025, 32}
    };
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        reset();
        framebuffer_info_t fb = {.buffer = (uint8_t *)1, .width = 1};
        CHECK(!ramfb_init(&fb, modes[i][0], modes[i][1], modes[i][2]), "reject invalid mode %u", i);
        CHECK(!fb.buffer && !fb.width && !fb.pitch, "failed init leaves unusable framebuffer");
        CHECK(!init_calls && !write_calls && !map_calls, "reject mode before hardware access");
    }
    reset();
    CHECK(!ramfb_init(NULL, 1024, 768, 32), "reject NULL info");
    CHECK(!init_calls, "NULL info never accesses hardware");
}

static void test_optional_device_and_failures(void) {
    for (unsigned failure = 0; failure < 6; failure++) {
        reset();
        switch (failure) {
        case 0: platform.fw_cfg_base = 0; break;
        case 1: init_ok = 0; break;
        case 2: file_ok = 0; break;
        case 3: file_size = 27; break;
        case 4: page_ok = 0; break;
        case 5: write_ok = 0; break;
        }
        framebuffer_info_t fb = {.buffer = (uint8_t *)1};
        CHECK(!ramfb_init(&fb, 1024, 768, 32), "handle failure %u", failure);
        CHECK(!fb.buffer, "failure leaves serial fallback possible");
        CHECK(write_calls == (failure == 5), "avoid DMA until dependencies pass");
    }
    reset();
    file_size = 29;
    framebuffer_info_t fb;
    CHECK(!ramfb_init(&fb, 1024, 768, 32) && !write_calls, "reject oversized ramfb config too");
}

static void test_wire_format_and_scanout(void) {
    reset();
    framebuffer_info_t fb;
    CHECK(ramfb_init(&fb, 1024, 768, 32), "configure default1024x768 mode");
    CHECK(fb.width == 1024 && fb.height == 768 && fb.depth == 32 && fb.pitch == 4096,
          "framebuffer geometry supports shared pixel API");
    CHECK(mapped_base == (uintptr_t)fb.buffer, "map the actual scanout buffer");
#ifndef __APPLE__
    // Mach-O caps host segment alignment at16KiB; kernel ELF has no such cap.
    CHECK(!(mapped_base & (0x200000 - 1)), "scanout owns aligned MMU blocks");
#endif
    CHECK(mapped_size == 0x400000, "scanout mapping covers full reservation");
    uint64_t address = (uint64_t)be32(0) << 32 | be32(4);
    CHECK(address == (uintptr_t)fb.buffer, "DMA address is64-bit big endian");
    CHECK(be32(8) == 0x34325258U, "DRM XRGB8888 preserves0x00RRGGBB pixel colors");
    CHECK(!be32(12) && be32(16) == 1024 && be32(20) == 768 && be32(24) == 4096,
          "wire flags, dimensions and stride are big endian");
    framebuffer_clear(&fb, 0x112233);
    CHECK(*(uint32_t *)fb.buffer == 0x112233 &&
          *(uint32_t *)(fb.buffer + fb.pitch * 767 + 1023 * 4) == 0x112233,
          "shared clear writes first and final pixels");
    framebuffer_put_pixel(&fb, 17, 19, 0x445566);
    CHECK(*(uint32_t *)(fb.buffer + 19 * fb.pitch + 17 * 4) == 0x445566, "shared put_pixel layout");
    framebuffer_put_pixel(&fb, fb.width, 0, 0);
    CHECK(*(uint32_t *)fb.buffer == 0x112233, "out-of-bounds pixel leaves scanout intact");
    reset();
    framebuffer_info_t maximum;
    CHECK(ramfb_init(&maximum, 1024, 1024, 32), "allow exact4MiB capacity");
    CHECK(maximum.buffer == fb.buffer, "scanout reservation remains stable across calls");
}

TEST_MAIN(test_invalid_modes, test_optional_device_and_failures, test_wire_format_and_scanout)
