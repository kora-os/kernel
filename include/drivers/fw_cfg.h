// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// QEMU's standard MMIO fw_cfg interface. DMA buffers must be in identity
// mapped, non-cacheable RAM. Keep buffers valid after a failed write too:
// a timeout does not prove DMA completion. ramfb uses permanent storage.
int fw_cfg_init(uintptr_t base);
int fw_cfg_find_file(const char *name, uint16_t *selector, uint32_t *size);
int fw_cfg_write(uint16_t selector, const void *buffer, uint32_t size);
