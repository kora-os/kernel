#include "drivers/mailbox.h"
#include "peripherals/mailbox.h"

static inline uint32_t arm_to_bus(uintptr_t addr) {
#ifdef QEMU_TESTING
    return (uint32_t)addr;
#else
#if RPI_VERSION == 4
    return (uint32_t)(addr + 0xC0000000u);
#else
    return (uint32_t)(addr + 0x40000000u);
#endif
#endif
}

static inline uint32_t mailbox_make_request(uint8_t channel, volatile uint32_t *buffer) {
    uint32_t addr = arm_to_bus((uintptr_t)buffer);
    return (uint32_t)((addr & ~0xFu) | (channel & 0xFu));
}

int mailbox_call(uint8_t channel, volatile uint32_t *buffer) {
    uint32_t request = mailbox_make_request(channel, buffer);
    uint32_t timeout = 0x100000;

    while ((REGS_MAILBOX->status & MAILBOX_STATUS_FULL) && timeout--) {
        // Wait for space
    }
    if (timeout == 0) {
        return 0;
    }
    REGS_MAILBOX->write = request;

    timeout = 0x100000;
    while (timeout--) {
        while ((REGS_MAILBOX->status & MAILBOX_STATUS_EMPTY) && timeout--) {
            // Wait for response
        }
        if (timeout == 0) {
            return 0;
        }
        uint32_t response = REGS_MAILBOX->read;
        if (response == request) {
            return buffer[1] == 0x80000000;
        }
    }

    return 0;
}
