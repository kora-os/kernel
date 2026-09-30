// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.h"

#define VIRT_MAX_VIRTIO 32
#define VIRT_MAX_RESERVED 16

struct virt_reserved_region {
    uintptr_t base;
    size_t size;
};

struct virt_mmio_device {
    uintptr_t base;
    unsigned irq;
    bool edge_triggered;
};

struct virt_platform {
    uintptr_t ram_base;
    size_t ram_size;
    uintptr_t dtb_base;
    size_t dtb_size;
    uintptr_t uart_base;
    unsigned uart_irq;
    uintptr_t gic_dist;
    uintptr_t gic_cpu;
    uintptr_t fw_cfg_base;
    unsigned reserved_count;
    struct virt_reserved_region reserved[VIRT_MAX_RESERVED];
    unsigned virtio_count;
    struct virt_mmio_device virtio[VIRT_MAX_VIRTIO];
};

// Direct-kernel boot passes the DTB in x0; ELF boot places it at RAM base.
bool virt_platform_init(uintptr_t dtb);
// Pure parser with an explicit readable bound, also used by host tests.
bool virt_platform_parse(const void *dtb, size_t available);
const struct virt_platform *virt_platform_get(void);
