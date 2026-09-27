#pragma once

#include "common.h"

#include "peripherals/base.h"

struct GpioPinData {
    reg32 reserved;
    reg32 data[2];
};

struct GpioRegs {
    reg32 func_select[6];
    struct GpioPinData output_set;
    struct GpioPinData output_clear;
    struct GpioPinData level;
    struct GpioPinData ev_detect_status;
    struct GpioPinData re_detect_enable;
    struct GpioPinData fe_detect_enable;
    struct GpioPinData hi_detect_enable;
    struct GpioPinData lo_detect_enable;
    struct GpioPinData async_re_detect;
    struct GpioPinData async_fe_detect;
    reg32 reserved;
    reg32 pupd_enable;             // GPPUD (BCM2835/Pi 3 pull control)
    reg32 pupd_enable_clocks[2];   // GPPUDCLK0/1
    reg32 reserved2[17];
    reg32 pup_pdn_cntrl[4];        // GPIO_PUP_PDN_CNTRL_REG0..3 (BCM2711/Pi 4), at 0xE4
};

_Static_assert(__builtin_offsetof(struct GpioRegs, pup_pdn_cntrl) == 0xE4,
               "BCM2711 pull registers start at GPIO base + 0xE4");

#define REGS_GPIO ((struct GpioRegs *)(PBASE + 0x00200000))
