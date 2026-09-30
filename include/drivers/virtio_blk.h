// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "fs/blkdev.h"
// NULL with present=false means no disk (ramdisk fallback). NULL with
// present=true means a configured disk failed and must not be silently hidden.
blkdev_t *virtio_blk_init(bool *present);
