// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "peripherals/base.h"

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

// Register map for the legacy Broadcom interrupt path used on Raspberry Pi 3
// (and the QEMU raspi3b machine): the BCM2835 ARM interrupt controller for
// peripheral (GPU) IRQs, plus the BCM2836 per-core local controller that routes
// the ARM generic-timer and GPU interrupts to a specific core.
//
// The Raspberry Pi 4 uses a GIC-400 instead; that path is added with the Pi 4
// bring-up and is selected at that point. Here we only define the legacy set.

// --- BCM2835 ARM interrupt controller (peripheral / GPU IRQs), at PBASE+0xB200.
#define IRQ_BASIC_PENDING  (PBASE + 0x0000B200)
#define IRQ_PENDING_1      (PBASE + 0x0000B204)  // IRQs 0..31
#define IRQ_PENDING_2      (PBASE + 0x0000B208)  // IRQs 32..63
#define FIQ_CONTROL        (PBASE + 0x0000B20C)
#define ENABLE_IRQS_1      (PBASE + 0x0000B210)
#define ENABLE_IRQS_2      (PBASE + 0x0000B214)
#define ENABLE_BASIC_IRQS  (PBASE + 0x0000B218)
#define DISABLE_IRQS_1     (PBASE + 0x0000B21C)
#define DISABLE_IRQS_2     (PBASE + 0x0000B220)
#define DISABLE_BASIC_IRQS (PBASE + 0x0000B224)

// --- BCM2836 local peripherals (per-core interrupt routing). The local base is
// 0x40000000 on the Pi 3 and the QEMU raspi3b/raspi4b machines, independent of
// PBASE.
#define LOCAL_BASE 0x40000000

#define LOCAL_CONTROL          (LOCAL_BASE + 0x000)
#define LOCAL_PRESCALER        (LOCAL_BASE + 0x008)
#define LOCAL_GPU_ROUTING      (LOCAL_BASE + 0x00C)
#define CORE0_TIMER_IRQCNTL    (LOCAL_BASE + 0x040)
#define CORE0_IRQ_SOURCE       (LOCAL_BASE + 0x060)

// CORE0_TIMER_IRQCNTL bits: route each generic-timer event to this core's IRQ.
#define LOCAL_TIMER_IRQ_CNTPNS BIT(1)  // non-secure physical timer (CNTP_EL0)

// CORE0_IRQ_SOURCE bits.
#define CORE_IRQ_CNTPS  BIT(0)
#define CORE_IRQ_CNTPNS BIT(1)
#define CORE_IRQ_CNTHP  BIT(2)
#define CORE_IRQ_CNTV   BIT(3)
#define CORE_IRQ_GPU    BIT(8)  // a peripheral IRQ is pending (read BCM2835 regs)

// --- KoraOS IRQ numbering ---------------------------------------------------
// Peripheral (GPU) IRQs occupy 0..63 exactly as in the BCM2835 pending
// registers; local per-core sources are placed above them. USB lives at
// peripheral IRQ 9 (needed when the USB stack calls ConnectIRQ later).
#define IRQ_PERIPH_USB    9
#define IRQ_LOCAL_BASE    64
#define IRQ_LOCAL_CNTPNS  (IRQ_LOCAL_BASE + 1)  // matches CORE_IRQ_CNTPNS bit
#define IRQ_COUNT         96
