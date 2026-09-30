#pragma once

#include "peripherals/gpio.h"

typedef enum _GpioFunc {
    GFInput = 0,
    GFOutput = 1,
    GFAlt0 = 4,
    GFAlt1 = 5,
    GFAlt2 = 6,
    GFAlt3 = 7,
    GFAlt4 = 3,
    GFAlt5 = 2
} GpioFunc;

typedef enum _GpioPull {
    GPNone,
    GPUp,
    GPDown
} GpioPull;

void gpio_pin_set_func(uint8_t pinNumber, GpioFunc func);

// Set a pin's pull resistor. The Pi 3 (BCM2835 GPPUD clocking sequence) and
// the Pi 4 (BCM2711 GPIO_PUP_PDN_CNTRL registers) do this differently.
void gpio_pin_set_pull(uint8_t pinNumber, GpioPull pull);

void gpio_pin_enable(uint8_t pinNumber);
