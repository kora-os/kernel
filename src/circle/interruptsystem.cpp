// SPDX-License-Identifier: GPL-3.0-or-later
//
// KoraOS bridge for Circle's CInterruptSystem. KoraOS owns the interrupt
// controller (arch/irq.c); this facade just routes Circle's ConnectIRQ to it.
// KoraOS numbers IRQs the way Circle does for the same board (peripherals/irq.h):
// VideoCore IRQs 0..63 on the Pi 3 (bcm2835int.h), GIC interrupt IDs on the Pi 4
// (bcm2711int.h). So no remapping is needed.

#include <circle/interrupt.h>

#include "arch/irq.h"

CInterruptSystem *CInterruptSystem::s_pThis = 0;

CInterruptSystem::CInterruptSystem(void) {
    s_pThis = this;
}

CInterruptSystem::~CInterruptSystem(void) {
    s_pThis = 0;
}

boolean CInterruptSystem::Initialize(void) {
    // KoraOS already brought up the controller (irq_init) in kernel_main; do not
    // re-initialize it here or the KoraOS system tick would be unhooked.
    return TRUE;
}

void CInterruptSystem::ConnectIRQ(unsigned nIRQ, TIRQHandler *pHandler,
                                  void *pParam) {
    irq_connect(nIRQ, (irq_handler_t)pHandler, pParam);
}

void CInterruptSystem::DisconnectIRQ(unsigned nIRQ) {
    irq_disconnect(nIRQ);
}

void CInterruptSystem::ConnectFIQ(unsigned nFIQ, TFIQHandler *pHandler,
                                  void *pParam) {
    // KoraOS delivers everything as IRQ; the USB stack only takes the FIQ path
    // under USE_USB_FIQ (not defined), so this is a fallback.
    irq_connect(nFIQ, (irq_handler_t)pHandler, pParam);
}

void CInterruptSystem::DisconnectFIQ(void) {
    // Single FIQ user at most; nothing to track.
}

CInterruptSystem *CInterruptSystem::Get(void) {
    return s_pThis;
}
