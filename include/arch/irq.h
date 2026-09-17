// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common.h"

// KoraOS interrupt handling: the CPU-side IRQ mask plus a small dispatcher over
// the platform interrupt controller (see peripherals/irq.h). This is KoraOS's
// own layer; the vendored Circle USB stack is bridged onto it rather than the
// other way around. IRQ numbers are the KoraOS scheme from peripherals/irq.h
// (peripheral/GPU IRQs 0..63, local per-core sources at IRQ_LOCAL_BASE+).

typedef void (*irq_handler_t)(void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the interrupt controller and clear the handler table. Does not unmask
// CPU interrupts; call irq_enable() once handlers are ready.
void irq_init(void);

// Register (and, for peripheral IRQs, enable in the controller) a handler for an
// IRQ. Local per-core sources (e.g. the generic timer) are additionally enabled
// by their own driver. Handlers run with CPU interrupts masked.
void irq_connect(unsigned irq, irq_handler_t handler, void *ctx);

// Unregister and, for peripheral IRQs, disable an IRQ in the controller.
void irq_disconnect(unsigned irq);

// Unmask / mask IRQs at the CPU (PSTATE.I / DAIF).
void irq_enable(void);
void irq_disable(void);

// Dispatch entry point called from the EL1/EL0 IRQ vectors. Not called directly.
void handle_irq(void);

#ifdef __cplusplus
}
#endif
