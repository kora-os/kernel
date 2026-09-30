// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/fw_cfg.h"

#ifdef KORAOS_VIRT
#include "mm/coherent.h"

#define FW_CFG_SIGNATURE 0x0000
#define FW_CFG_ID 0x0001
#define FW_CFG_FILE_DIR 0x0019
#define FW_CFG_DMA_FEATURE (1U << 1)
#define FW_CFG_DMA_ERROR (1U << 0)
#define FW_CFG_DMA_SELECT (1U << 3)
#define FW_CFG_DMA_WRITE (1U << 4)
#define FW_CFG_MAX_FILES 1024U
#define FW_CFG_DMA_POLLS 1000000U
// Slots 68..127 are reserved for KoraOS; Pi mailbox uses slot 127.
#define COHERENT_SLOT_FW_CFG_DMA 125

struct fw_cfg_dma {
    uint32_t control;
    uint32_t length;
    uint64_t address;
} __attribute__((packed));

_Static_assert(sizeof(struct fw_cfg_dma) == 16, "fw_cfg DMA descriptor size");

static uintptr_t fw_cfg_base;
static int dma_poisoned;

static uint16_t to_be16(uint16_t value) {
    return __builtin_bswap16(value);
}

static uint32_t to_be32(uint32_t value) {
    return __builtin_bswap32(value);
}

static uint64_t to_be64(uint64_t value) {
    return __builtin_bswap64(value);
}

static void select_item(uint16_t selector) {
    *(volatile uint16_t *)(fw_cfg_base + 8) = to_be16(selector);
    asm volatile("dmb osh" ::: "memory");
}

static void read_bytes(void *buffer, unsigned count) {
    uint8_t *bytes = buffer;
    volatile const uint8_t *data = (volatile const uint8_t *)fw_cfg_base;
    for (unsigned i = 0; i < count; i++) {
        bytes[i] = *data;
    }
}

int fw_cfg_init(uintptr_t base) {
    fw_cfg_base = 0;
    if (!base || dma_poisoned) {
        return 0;
    }
    fw_cfg_base = base;
    uint8_t signature[4];
    select_item(FW_CFG_SIGNATURE);
    read_bytes(signature, sizeof(signature));
    if (signature[0] != 'Q' || signature[1] != 'E' ||
        signature[2] != 'M' || signature[3] != 'U') {
        fw_cfg_base = 0;
        return 0;
    }
    uint8_t features[4];
    select_item(FW_CFG_ID);
    read_bytes(features, sizeof(features));
    // Unlike the directory and MMIO selector, the ID bitmap is little-endian.
    uint32_t id = (uint32_t)features[0] | (uint32_t)features[1] << 8 |
                  (uint32_t)features[2] << 16 | (uint32_t)features[3] << 24;
    if (!(id & FW_CFG_DMA_FEATURE)) {
        fw_cfg_base = 0;
        return 0;
    }
    return 1;
}

static int name_matches(const char *name, const uint8_t *entry_name) {
    for (unsigned i = 0; i < 56; i++) {
        if ((uint8_t)name[i] != entry_name[i]) {
            return 0;
        }
        if (!entry_name[i]) {
            return 1;
        }
    }
    return 0; // file names must be NUL-terminated within the directory record
}

int fw_cfg_find_file(const char *name, uint16_t *selector, uint32_t *size) {
    if (!fw_cfg_base || !name || !selector || !size) {
        return 0;
    }
    select_item(FW_CFG_FILE_DIR);
    uint32_t count;
    read_bytes(&count, sizeof(count));
    count = to_be32(count);
    if (count > FW_CFG_MAX_FILES) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t entry[64];
        read_bytes(entry, sizeof(entry));
        if (!name_matches(name, entry + 8)) {
            continue;
        }
        uint16_t key = (uint16_t)entry[4] << 8 | entry[5];
        if (key < 0x20 || key >= 0x4000 || entry[6] || entry[7]) {
            return 0;
        }
        *selector = key;
        *size = (uint32_t)entry[0] << 24 | (uint32_t)entry[1] << 16 |
                (uint32_t)entry[2] << 8 | entry[3];
        return 1;
    }
    return 0;
}

int fw_cfg_write(uint16_t selector, const void *buffer, uint32_t size) {
    if (!fw_cfg_base || !buffer || !size || selector < 0x20 || selector >= 0x4000) {
        return 0;
    }
    volatile struct fw_cfg_dma *dma = coherent_page(COHERENT_SLOT_FW_CFG_DMA);
    if (!dma) {
        return 0;
    }
    dma->control = to_be32((uint32_t)selector << 16 |
                           FW_CFG_DMA_SELECT | FW_CFG_DMA_WRITE);
    dma->length = to_be32(size);
    dma->address = to_be64((uintptr_t)buffer);
    asm volatile("dsb oshst" ::: "memory");
    // High half first: writing the low half triggers the transfer.
    uintptr_t address = (uintptr_t)dma;
    *(volatile uint32_t *)(fw_cfg_base + 16) = to_be32(address >> 32);
    *(volatile uint32_t *)(fw_cfg_base + 20) = to_be32((uint32_t)address);
    asm volatile("dsb osh" ::: "memory");
    for (unsigned i = 0; i < FW_CFG_DMA_POLLS; i++) {
        uint32_t control = to_be32(dma->control);
        if (control & FW_CFG_DMA_ERROR) {
            return 0;
        }
        if (!control) {
            asm volatile("dmb osh" ::: "memory");
            return 1;
        }
    }
    // A timed-out descriptor may still belong to the device: never reuse it.
    dma_poisoned = 1;
    fw_cfg_base = 0;
    return 0;
}
#endif
