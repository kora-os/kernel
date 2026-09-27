// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "peripherals/base.h"

#ifndef BIT
#define BIT(x) (1UL << (x))
#endif

// Interrupt controller registers and the KoraOS IRQ numbering for each board.
//
// - Raspberry Pi 3 (and QEMU raspi3b): the legacy BCM2835 ARM interrupt
//   controller for peripheral (VideoCore) IRQs, plus the BCM2836 per-core local
//   controller that routes the generic timer and the peripheral IRQs to a core.
// - Raspberry Pi 4: a GIC-400. The firmware enables it by default (config.txt
//   also sets enable_gic=1) and then the legacy controller is not used.
//
// KoraOS IRQ numbers are the numbers Circle uses for the same board
// (circle/bcm2835int.h, circle/bcm2711int.h), so the vendored USB stack's
// ConnectIRQ numbers pass straight through: 0..63 = VideoCore IRQs on the Pi 3,
// GIC interrupt IDs on the Pi 4. Drivers use the names at the bottom, not raw
// numbers.

#if RPI_VERSION == 4

// --- GIC-400 (BCM2711), in the ARM local peripheral block at 0xFF800000.
#define GICD_BASE 0xFF841000  // distributor
#define GICC_BASE 0xFF842000  // CPU interface

#define GICD_CTLR        (GICD_BASE + 0x000)
#define GICD_TYPER       (GICD_BASE + 0x004)
#define GICD_ISENABLER0  (GICD_BASE + 0x100)  // 1 bit per IRQ
#define GICD_ICENABLER0  (GICD_BASE + 0x180)
#define GICD_ICPENDR0    (GICD_BASE + 0x280)
#define GICD_ICACTIVER0  (GICD_BASE + 0x380)
#define GICD_IPRIORITYR0 (GICD_BASE + 0x400)  // 8 bits per IRQ
#define GICD_ITARGETSR0  (GICD_BASE + 0x800)  // 8 bits per IRQ (CPU mask)
#define GICD_ICFGR0      (GICD_BASE + 0xC00)  // 2 bits per IRQ

#define GICC_CTLR (GICC_BASE + 0x000)
#define GICC_PMR  (GICC_BASE + 0x004)
#define GICC_IAR  (GICC_BASE + 0x00C)
#define GICC_EOIR (GICC_BASE + 0x010)

#define GICD_CTLR_ENABLE       BIT(0)
#define GICC_CTLR_ENABLE       BIT(0)
#define GICC_PMR_ALLOW_ALL     0xF0   // lowest priority mask: nothing filtered
#define GIC_PRIORITY_DEFAULT   0xA0
#define GIC_TARGET_CORE0       0x01
#define GICC_IAR_ID_MASK       0x3FF
#define GIC_SPURIOUS           1023   // IAR value when nothing is pending

// GIC interrupt IDs: 0..15 SGIs, 16..31 per-core PPIs, 32+ shared SPIs. The
// VideoCore peripheral IRQs 0..63 are SPIs 64..127 on the BCM2711.
#define GIC_PPI(n) (16 + (n))
#define GIC_SPI(n) (32 + (n))

#define IRQ_VC(n)        GIC_SPI(64 + (n))
#define IRQ_TIMER_CNTPNS GIC_PPI(14)  // non-secure physical timer (CNTP_EL0)
#define IRQ_COUNT        256

#else  // Raspberry Pi 3

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

// --- BCM2836 local peripherals (per-core interrupt routing), at 0x40000000 on
// the Pi 3 and QEMU raspi3b, independent of PBASE. (The Pi 4's local block is
// at 0xFF800000 and its timer interrupts go through the GIC instead.)
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

// Peripheral IRQs occupy 0..63 exactly as in the BCM2835 pending registers;
// local per-core sources are placed above them, in CORE0_IRQ_SOURCE bit order.
#define IRQ_VC(n)        (n)
#define IRQ_LOCAL_BASE   64
#define IRQ_TIMER_CNTPNS (IRQ_LOCAL_BASE + 1)  // matches CORE_IRQ_CNTPNS bit
#define IRQ_COUNT        96

#endif

// --- Board-independent names for the IRQs KoraOS uses.
#define IRQ_USB   IRQ_VC(9)   // DWC2 USB controller (Pi 3)
#define IRQ_AUX   IRQ_VC(29)  // mini-UART (shared with SPI1/SPI2)
#define IRQ_UART0 IRQ_VC(57)  // PL011
