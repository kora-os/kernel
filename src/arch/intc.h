// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The platform interrupt controller behind arch/irq.c, private to src/arch.
// One implementation per board, selected by RPI_VERSION: irq_bcm.c (Pi 3,
// legacy BCM2835/BCM2836 controllers) and irq_gic.c (Pi 4, GIC-400). IRQ
// numbers are the KoraOS numbers from peripherals/irq.h.

// Controller name, for diagnostics.
extern const char intc_name[];

// Bring the controller up with every IRQ disabled and nothing pending.
void intc_init(void);

// Enable / disable one IRQ at the controller (routing it to core 0).
void intc_enable(unsigned irq);
void intc_disable(unsigned irq);

// Called from handle_irq(): acknowledge every pending IRQ and pass each one to
// irq_dispatch().
void intc_handle(void);

// Provided by arch/irq.c: run the registered handler for one IRQ.
void irq_dispatch(unsigned irq);
