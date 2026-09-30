#pragma once

#include "common.h"
#include "peripherals/base.h"

// ARM PL011 UART registers (subset)
struct Pl011Regs {
    reg32 dr;         // 0x00 Data Register
    reg32 rsrecr;     // 0x04 Receive Status / Error Clear
    reg32 reserved0[4];
    reg32 fr;         // 0x18 Flag Register
    reg32 reserved1;  // 0x1C
    reg32 ilpr;       // 0x20 IrDA Low-Power Counter Register (unused)
    reg32 ibrd;       // 0x24 Integer Baud Rate Divisor
    reg32 fbrd;       // 0x28 Fractional Baud Rate Divisor
    reg32 lcrh;       // 0x2C Line Control Register
    reg32 cr;         // 0x30 Control Register
    reg32 ifls;       // 0x34 Interrupt FIFO Level Select Register
    reg32 imsc;       // 0x38 Interrupt Mask Set/Clear Register
    reg32 ris;        // 0x3C Raw Interrupt Status Register
    reg32 mis;        // 0x40 Masked Interrupt Status Register
    reg32 icr;        // 0x44 Interrupt Clear Register
};

// PL011 base address on Raspberry Pi (UART0)
#ifdef KORAOS_VIRT
#include "platform/virt.h"
#define REGS_PL011 ((struct Pl011Regs *)virt_platform_get()->uart_base)
#else
#define REGS_PL011 ((struct Pl011Regs *)(PBASE + 0x00201000))
#endif

