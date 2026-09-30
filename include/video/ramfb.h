// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "video/framebuffer.h"

// Configure optional QEMU ramfb scanout via fw_cfg; only 32-bit XRGB is supported.
int ramfb_init(framebuffer_info_t *fb, uint32_t width, uint32_t height, uint32_t depth);
