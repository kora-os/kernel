// SPDX-License-Identifier: GPL-3.0-or-later
#include "drivers/virtio_input.h"

#if defined(KORAOS_VIRT) && defined(KORAOS_VIRTIO_INPUT)
#include "arch/irq.h"
#include "drivers/virtio.h"
#include "mm/coherent.h"
#include "platform/virt.h"
#include "tty.h"

#define INPUT_CFG_EV_BITS 0x11
#define INPUT_EV_KEY 1
#define KEY_ENTER 28
#define KEY_A 30
#define INPUT_CONFIG_ATTEMPTS 8

struct input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};
_Static_assert(sizeof(struct input_event) == 8, "VirtIO input event ABI");

static struct {
    struct virtio_device device;
    struct virtio_queue events;
    struct virtio_queue status;
    volatile struct input_event *buffers;
    uint8_t buffer_by_head[VIRTIO_QUEUE_MAX];
    struct virtio_keymap_state keymap;
    unsigned irq;
    bool attempted;
    bool active;
} keyboard;

// keyboard_lock (IRQ-safe) serializes queue setup against the interrupt
// handler. Host tests run single-threaded without it.
#ifdef VIRTIO_INPUT_HOST_TEST
static uint64_t keyboard_lock_take(void) { return 0; }
static void keyboard_lock_drop(uint64_t flags) { (void)flags; }
#else
#include "arch/spinlock.h"
static struct spinlock keyboard_lock = SPINLOCK_INIT("virtio keyboard");
static uint64_t keyboard_lock_take(void) { return spin_lock_irqsave(&keyboard_lock); }
static void keyboard_lock_drop(uint64_t flags) { spin_unlock_irqrestore(&keyboard_lock, flags); }
#endif

static void barrier(void) {
#ifdef VIRTIO_INPUT_HOST_TEST
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#else
    asm volatile("dmb osh" ::: "memory");
#endif
}

static uint32_t read_reg(unsigned offset) {
    return *(volatile uint32_t *)(keyboard.device.base + offset);
}

static void write_reg(unsigned offset, uint32_t value) {
    *(volatile uint32_t *)(keyboard.device.base + offset) = value;
}

static bool is_keyboard(void) {
    volatile uint8_t *config = (volatile uint8_t *)(keyboard.device.base + 0x100);
    for (unsigned attempt = 0; attempt < INPUT_CONFIG_ATTEMPTS; attempt++) {
        uint32_t before = virtio_config_generation(&keyboard.device);
        config[0] = INPUT_CFG_EV_BITS;
        config[1] = INPUT_EV_KEY;
        barrier();
        uint8_t size = config[2];
        // This driver requires the alphabet and Enter, not pointer buttons.
        bool supported = false;
        if (size >= 4 && size <= 128) {
            uint8_t keys = config[8 + KEY_A / 8];
            supported = (keys & (1u << (KEY_A % 8))) &&
                        (keys & (1u << (KEY_ENTER % 8)));
        }
        if (before == virtio_config_generation(&keyboard.device)) {
            return supported;
        }
    }
    return false;
}

static bool post_event(unsigned buffer_index) {
    struct virtio_buffer buffer = {
        .address = (uintptr_t)&keyboard.buffers[buffer_index],
        .length = sizeof(struct input_event),
        .device_writes = true
    };
    uint16_t head;
    if (virtio_queue_submit(&keyboard.events, &buffer, 1, &head) ||
        head >= keyboard.events.size) {
        return false;
    }
    keyboard.buffer_by_head[head] = buffer_index;
    return true;
}

static void stop_keyboard(void) {
    keyboard.active = false;
    virtio_device_fail(&keyboard.device);
    irq_disconnect(keyboard.irq);
    // Queues and event buffers remain reserved: the device may still own them.
}

static void keyboard_events(void);

static void keyboard_irq(void *ctx) {
    (void)ctx;
    uint64_t flags = keyboard_lock_take();
    keyboard_events();
    keyboard_lock_drop(flags);
}

static void keyboard_events(void) {
    if (!keyboard.active) {
        return;
    }
    uint32_t interrupts = read_reg(0x60);
    if (interrupts) {
        write_reg(0x64, interrupts);
        barrier();
    }
    if ((interrupts & 2) && !is_keyboard()) {
        stop_keyboard();
        return;
    }
    // Work per IRQ is bounded; newly completed buffers reassert the IRQ since
    // only the status seen on entry was acknowledged. No allocation occurs.
    for (unsigned i = 0; i < keyboard.events.size; i++) {
        uint16_t head;
        uint32_t length;
        int result = virtio_queue_poll(&keyboard.events, &head, &length);
        if (!result) {
            return;
        }
        if (result < 0 || length != sizeof(struct input_event) ||
            head >= keyboard.events.size ||
            keyboard.buffer_by_head[head] >= keyboard.events.size) {
            stop_keyboard();
            return;
        }
        unsigned index = keyboard.buffer_by_head[head];
        volatile const struct input_event *event = &keyboard.buffers[index];
        struct virtio_keymap_result key;
        virtio_keymap_event(&keyboard.keymap, event->type, event->code, event->value, &key);
        for (unsigned j = 0; j < key.length; j++) {
            tty_input_push(key.bytes[j]);
        }
        if (key.scroll) {
            tty_scrollback(key.scroll);
        }
        if (!post_event(index)) {
            stop_keyboard();
            return;
        }
    }
}

static bool initialize_keyboard(void) {
    const struct virt_platform *platform = virt_platform_get();
    if (!platform) {
        return false;
    }
    for (unsigned i = 0; i < platform->virtio_count; i++) {
        const struct virt_mmio_device *candidate = &platform->virtio[i];
        if (candidate->irq >= irq_lines() ||
            !virtio_device_init(&keyboard.device, candidate->base, VIRTIO_DEVICE_INPUT, 0, 0)) {
            continue;
        }
        if (!is_keyboard()) {
            virtio_device_fail(&keyboard.device);
            continue;
        }
        keyboard.irq = candidate->irq;
        if (!virtio_queue_init(&keyboard.events, &keyboard.device, 0, VIRTIO_INPUT_QUEUE_SLOT) ||
            !virtio_queue_init(&keyboard.status, &keyboard.device, 1, VIRTIO_INPUT_QUEUE_SLOT + 1)) {
            virtio_device_fail(&keyboard.device);
            return false;
        }
        keyboard.buffers = coherent_page(VIRTIO_INPUT_BUFFER_SLOT);
        if (!keyboard.buffers) {
            virtio_device_fail(&keyboard.device);
            return false;
        }
        // The transport defaults to polling. Input instead requests used IRQs.
        keyboard.events.avail->flags = 0;
        barrier();
        for (unsigned j = 0; j < VIRTIO_QUEUE_MAX; j++) {
            keyboard.buffer_by_head[j] = 0xFF;
        }
        for (unsigned j = 0; j < keyboard.events.size; j++) {
            if (!post_event(j)) {
                virtio_device_fail(&keyboard.device);
                return false;
            }
        }
        irq_connect(keyboard.irq, keyboard_irq, NULL);
        if (!virtio_device_ready(&keyboard.device)) {
            stop_keyboard();
            return false;
        }
        keyboard.active = true;
        virtio_queue_notify(&keyboard.events);
        return true;
    }
    return false;
}

bool virtio_input_init(void) {
    // Kernel bring-up already has systick IRQs enabled: serialize queue setup
    // against the interrupt handler.
    uint64_t flags = keyboard_lock_take();
    bool result = keyboard.active;
    if (!keyboard.attempted) {
        keyboard.attempted = true;
        result = initialize_keyboard();
    }
    keyboard_lock_drop(flags);
    return result;
}
#endif
