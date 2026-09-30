// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "common.h"
#define VIRTIO_DEVICE_BLOCK 2u
#define VIRTIO_DEVICE_INPUT 18u
#define VIRTIO_F_VERSION_1 (1UL << 32)
#define VIRTIO_QUEUE_MAX 32u
#define VIRTIO_BLOCK_QUEUE_SLOT 80u
#define VIRTIO_BLOCK_REQUEST_SLOT 81u
#define VIRTIO_INPUT_QUEUE_SLOT 82u
#define VIRTIO_INPUT_BUFFER_SLOT 84u
struct virtio_device {
    uintptr_t base;
    uint64_t features;
    bool failed;
};
struct virtio_desc {
    uint64_t address;
    uint32_t length;
    uint16_t flags, next;
};
struct virtio_avail {
    uint16_t flags, index, ring[VIRTIO_QUEUE_MAX];
};
struct virtio_used_element {
    uint32_t id, length;
};
struct virtio_used {
    uint16_t flags, index;
    struct virtio_used_element ring[VIRTIO_QUEUE_MAX];
};
struct virtio_queue {
    struct virtio_device *device;
    volatile struct virtio_desc *desc;
    volatile struct virtio_avail *avail;
    volatile struct virtio_used *used;
    uint32_t allocated, pending, chain_mask[VIRTIO_QUEUE_MAX];
    uint16_t size, number, last_used, outstanding;
};
struct virtio_buffer {
    uintptr_t address;
    uint32_t length;
    bool device_writes;
};
// Modern MMIO only. required/optional omit unsupported transport features.
bool virtio_device_init(struct virtio_device *device, uintptr_t base, unsigned device_id,
                        uint64_t required, uint64_t optional);
bool virtio_queue_init(struct virtio_queue *queue, struct virtio_device *device, unsigned number,
                       unsigned coherent_slot);
bool virtio_device_ready(struct virtio_device *device);
void virtio_device_fail(struct virtio_device *device);
uint32_t virtio_config_read32(struct virtio_device *device, unsigned offset);
uint32_t virtio_config_generation(struct virtio_device *device);
// Single CPU, externally serialized. Buffers remain alive until completion.
// Callers maintain cached DMA payloads. Failed queues retain their memory.
int virtio_queue_submit(struct virtio_queue *queue, const struct virtio_buffer *buffers,
                        unsigned count, uint16_t *head);
void virtio_queue_notify(struct virtio_queue *queue);
// 1: completion, 0: none, -1: failed/corrupt. Completed chain is released.
int virtio_queue_poll(struct virtio_queue *queue, uint16_t *head, uint32_t *length);
