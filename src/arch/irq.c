// SPDX-License-Identifier: GPL-3.0-or-later
//
// Legacy Broadcom interrupt controller dispatch for KoraOS (Pi 3 / raspi3b).
// See arch/irq.h and peripherals/irq.h.

#include "arch/irq.h"

#include "memory_access.h"
#include "peripherals/irq.h"

static struct {
    irq_handler_t handler;
    void *ctx;
} irq_table[IRQ_COUNT];

void irq_init(void) {
    for (unsigned i = 0; i < IRQ_COUNT; i++) {
        irq_table[i].handler = NULL;
        irq_table[i].ctx = NULL;
    }
    // Mask every peripheral IRQ to start from a known-quiet state.
    write32(DISABLE_IRQS_1, 0xFFFFFFFF);
    write32(DISABLE_IRQS_2, 0xFFFFFFFF);
    write32(DISABLE_BASIC_IRQS, 0xFFFFFFFF);
    asm volatile("dsb sy" ::: "memory");
}

static void enable_peripheral(unsigned irq) {
    if (irq < 32) {
        write32(ENABLE_IRQS_1, 1u << irq);
    } else if (irq < 64) {
        write32(ENABLE_IRQS_2, 1u << (irq - 32));
    }
    asm volatile("dsb sy" ::: "memory");
}

static void disable_peripheral(unsigned irq) {
    if (irq < 32) {
        write32(DISABLE_IRQS_1, 1u << irq);
    } else if (irq < 64) {
        write32(DISABLE_IRQS_2, 1u << (irq - 32));
    }
    asm volatile("dsb sy" ::: "memory");
}

void irq_connect(unsigned irq, irq_handler_t handler, void *ctx) {
    if (irq >= IRQ_COUNT) {
        return;
    }
    irq_table[irq].handler = handler;
    irq_table[irq].ctx = ctx;
    if (irq < 64) {
        enable_peripheral(irq);
    }
}

void irq_disconnect(unsigned irq) {
    if (irq >= IRQ_COUNT) {
        return;
    }
    if (irq < 64) {
        disable_peripheral(irq);
    }
    irq_table[irq].handler = NULL;
    irq_table[irq].ctx = NULL;
}

static void dispatch(unsigned irq) {
    if (irq < IRQ_COUNT && irq_table[irq].handler != NULL) {
        irq_table[irq].handler(irq_table[irq].ctx);
    }
}

static void dispatch_pending(uint32_t pending, unsigned base) {
    while (pending != 0) {
        unsigned bit = (unsigned)__builtin_ctz(pending);
        dispatch(base + bit);
        pending &= ~(1u << bit);
    }
}

void handle_irq(void) {
    uint32_t source = read32(CORE0_IRQ_SOURCE);

    // Local per-core sources: generic-timer events (bits 0..3).
    dispatch_pending(source & 0xF, IRQ_LOCAL_BASE);

    // Peripheral (GPU) IRQs are signalled by one aggregate bit; the specific
    // lines come from the BCM2835 pending registers.
    if (source & CORE_IRQ_GPU) {
        dispatch_pending(read32(IRQ_PENDING_1), 0);
        dispatch_pending(read32(IRQ_PENDING_2), 32);
    }
}

void irq_enable(void) {
    asm volatile("msr daifclr, #2" ::: "memory");  // clear PSTATE.I
}

void irq_disable(void) {
    asm volatile("msr daifset, #2" ::: "memory");  // set PSTATE.I
}
