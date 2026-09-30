// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio.h"
#ifdef KORAOS_VIRT
#include "mm/coherent.h"
#define STATUS_ACK 1u
#define STATUS_DRIVER 2u
#define STATUS_DRIVER_OK 4u
#define STATUS_FEATURES_OK 8u
#define STATUS_NEEDS_RESET 64u
#define STATUS_FAILED 128u
#define RESET_POLLS 100000u
#define DESC_NEXT 1u
#define DESC_WRITE 2u
_Static_assert(sizeof(struct virtio_desc) == 16, "VirtIO descriptor ABI");
_Static_assert(sizeof(struct virtio_used_element) == 8, "VirtIO used element ABI");
static uint32_t read_reg(const struct virtio_device *device, unsigned offset) {
    return *(volatile uint32_t *)(device->base + offset);
}
static void write_reg(const struct virtio_device *device, unsigned offset, uint32_t value) {
    *(volatile uint32_t *)(device->base + offset) = value;
}
static void barrier(void) {
#ifdef VIRTIO_HOST_TEST
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    asm volatile("dmb osh" ::: "memory");
#endif
}
void virtio_device_fail(struct virtio_device *device) {
    device->failed = true;
    write_reg(device, 0x70, read_reg(device, 0x70) | STATUS_FAILED);
    barrier();
}
static bool device_live(struct virtio_device *device) {
    if (device->failed)
        return false;
    if (read_reg(device, 0x70) & (STATUS_NEEDS_RESET | STATUS_FAILED)) {
        virtio_device_fail(device);
        return false;
    }
    return true;
}
bool virtio_device_init(struct virtio_device *device, uintptr_t base, unsigned device_id,
                        uint64_t required, uint64_t optional) {
    device->base = base;
    device->features = 0;
    device->failed = false;
    if (!base || read_reg(device, 0) != 0x74726976u || read_reg(device, 4) != 2 ||
        read_reg(device, 8) != device_id) {
        device->failed = true;
        return false;
    }
    write_reg(device, 0x70, 0);
    barrier();
    unsigned poll;
    for (poll = 0; poll < RESET_POLLS; poll++) {
        if (!read_reg(device, 0x70))
            break;
    }
    if (poll == RESET_POLLS) {
        virtio_device_fail(device);
        return false;
    }
    write_reg(device, 0x70, STATUS_ACK);
    write_reg(device, 0x70, STATUS_ACK | STATUS_DRIVER);
    write_reg(device, 0x14, 0);
    barrier();
    uint64_t features = read_reg(device, 0x10);
    write_reg(device, 0x14, 1);
    barrier();
    features |= (uint64_t)read_reg(device, 0x10) << 32;
    required |= VIRTIO_F_VERSION_1;
    if ((features & required) != required) {
        virtio_device_fail(device);
        return false;
    }
    device->features = required | (features & optional);
    write_reg(device, 0x24, 0);
    write_reg(device, 0x20, (uint32_t)device->features);
    write_reg(device, 0x24, 1);
    write_reg(device, 0x20, (uint32_t)(device->features >> 32));
    barrier();
    write_reg(device, 0x70, STATUS_ACK | STATUS_DRIVER | STATUS_FEATURES_OK);
    barrier();
    if (!(read_reg(device, 0x70) & STATUS_FEATURES_OK) || !device_live(device)) {
        virtio_device_fail(device);
        return false;
    }
    return true;
}
static void write_address(struct virtio_device *device, unsigned offset, const volatile void *ptr) {
    uintptr_t address = (uintptr_t)ptr;
    write_reg(device, offset, (uint32_t)address);
    write_reg(device, offset + 4, (uint32_t)(address >> 32));
}
bool virtio_queue_init(struct virtio_queue *queue, struct virtio_device *device, unsigned number,
                       unsigned coherent_slot) {
    if (!device_live(device) || number > 65535)
        return false;
    write_reg(device, 0x30, number);
    barrier();
    if (read_reg(device, 0x44))
        return false;
    uint32_t maximum = read_reg(device, 0x34);
    if (!maximum)
        return false;
    unsigned size = VIRTIO_QUEUE_MAX;
    while (size > maximum)
        size >>= 1;
    uint8_t *page = coherent_page(coherent_slot);
    if (!page)
        return false;
    for (unsigned i = 0; i < 4096; i++)
        page[i] = 0;
    queue->device = device;
    queue->desc = (volatile struct virtio_desc *)page;
    queue->avail = (volatile struct virtio_avail *)(page + 1024);
    queue->used = (volatile struct virtio_used *)(page + 2048);
    queue->size = size;
    queue->number = number;
    queue->last_used = queue->outstanding = 0;
    queue->allocated = queue->pending = 0;
    for (unsigned i = 0; i < VIRTIO_QUEUE_MAX; i++)
        queue->chain_mask[i] = 0;
    // Polling transport: suppress used-buffer interrupts (advisory).
    queue->avail->flags = 1;
    write_reg(device, 0x38, size);
    write_address(device, 0x80, queue->desc);
    write_address(device, 0x90, queue->avail);
    write_address(device, 0xa0, queue->used);
    barrier();
    write_reg(device, 0x44, 1);
    barrier();
    return read_reg(device, 0x44) == 1;
}
bool virtio_device_ready(struct virtio_device *device) {
    if (!device_live(device))
        return false;
    write_reg(device, 0x70, read_reg(device, 0x70) | STATUS_DRIVER_OK);
    barrier();
    return device_live(device);
}
uint32_t virtio_config_read32(struct virtio_device *device, unsigned offset) {
    return read_reg(device, 0x100 + offset);
}
uint32_t virtio_config_generation(struct virtio_device *device) {
    barrier();
    return read_reg(device, 0xfc);
}
int virtio_queue_submit(struct virtio_queue *queue, const struct virtio_buffer *buffers,
                        unsigned count, uint16_t *head) {
    if (!device_live(queue->device) || !buffers || !head || !count || count > queue->size)
        return -1;
    uint16_t indices[VIRTIO_QUEUE_MAX];
    unsigned found = 0;
    uint32_t mask = 0;
    for (unsigned i = 0; i < queue->size && found < count; i++) {
        if (!(queue->allocated & (1u << i))) {
            indices[found++] = i;
            mask |= 1u << i;
        }
    }
    if (found != count)
        return -1;
    for (unsigned i = 0; i < count; i++) {
        if (!buffers[i].address || !buffers[i].length)
            return -1;
    }
    for (unsigned i = 0; i < count; i++) {
        volatile struct virtio_desc *desc = &queue->desc[indices[i]];
        desc->address = buffers[i].address;
        desc->length = buffers[i].length;
        desc->flags = (buffers[i].device_writes ? DESC_WRITE : 0) | (i + 1 < count ? DESC_NEXT : 0);
        desc->next = i + 1 < count ? indices[i + 1] : 0;
    }
    *head = indices[0];
    queue->allocated |= mask;
    queue->pending |= 1u << *head;
    queue->chain_mask[*head] = mask;
    queue->outstanding++;
    uint16_t index = queue->avail->index;
    queue->avail->ring[index % queue->size] = *head;
    barrier();
    queue->avail->index = (uint16_t)(index + 1);
    barrier();
    if (read_reg(queue->device, 0x70) & STATUS_DRIVER_OK) {
        virtio_queue_notify(queue);
    }
    return 0;
}
void virtio_queue_notify(struct virtio_queue *queue) {
    barrier();
    write_reg(queue->device, 0x50, queue->number);
}
int virtio_queue_poll(struct virtio_queue *queue, uint16_t *head, uint32_t *length) {
    if (!head || !length || !device_live(queue->device))
        return -1;
    uint16_t index = queue->used->index;
    barrier();
    uint16_t ready = (uint16_t)(index - queue->last_used);
    if (!ready)
        return 0;
    if (ready > queue->outstanding) {
        virtio_device_fail(queue->device);
        return -1;
    }
    volatile struct virtio_used_element *element =
        &queue->used->ring[queue->last_used % queue->size];
    uint32_t id = element->id;
    if (id >= queue->size || !(queue->pending & (1u << id))) {
        virtio_device_fail(queue->device);
        return -1;
    }
    *head = id;
    *length = element->length;
    queue->pending &= ~(1u << id);
    queue->allocated &= ~queue->chain_mask[id];
    queue->chain_mask[id] = 0;
    queue->outstanding--;
    queue->last_used++;
    uint32_t interrupts = read_reg(queue->device, 0x60);
    if (interrupts && (queue->avail->flags & 1)) {
        write_reg(queue->device, 0x64, interrupts);
    }
    return 1;
}
#endif
