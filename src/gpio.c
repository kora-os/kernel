#include "gpio.h"
#include "utils.h"

void gpio_pin_set_func(uint8_t pinNumber, GpioFunc func) {
    uint8_t bitStart = (pinNumber * 3) % 30;
    uint8_t reg = pinNumber / 10;

    uint32_t selector = REGS_GPIO->func_select[reg];
    selector &= ~(7 << bitStart);
    selector |= (func << bitStart);

    REGS_GPIO->func_select[reg] = selector;
}

void gpio_pin_set_pull(uint8_t pinNumber, GpioPull pull) {
#if RPI_VERSION == 4
    // Two bits per pin, 16 pins per register: 0 none, 1 up, 2 down.
    static const uint32_t codes[] = {[GPNone] = 0, [GPUp] = 1, [GPDown] = 2};
    uint8_t reg = pinNumber / 16;
    uint8_t shift = (pinNumber % 16) * 2;
    uint32_t value = REGS_GPIO->pup_pdn_cntrl[reg];
    value &= ~(3u << shift);
    value |= codes[pull] << shift;
    REGS_GPIO->pup_pdn_cntrl[reg] = value;
#else
    // Latch the mode (0 none, 1 down, 2 up) into the pin by clocking it, with
    // the 150-cycle setup and hold times the BCM2835 datasheet asks for.
    static const uint32_t codes[] = {[GPNone] = 0, [GPUp] = 2, [GPDown] = 1};
    REGS_GPIO->pupd_enable = codes[pull];
    delay(150);
    REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 1u << (pinNumber % 32);
    delay(150);
    REGS_GPIO->pupd_enable = 0;
    REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 0;
#endif
}

void gpio_pin_enable(uint8_t pinNumber) {
    REGS_GPIO->pupd_enable = 0;
    delay(150);
    REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 1 << (pinNumber % 32);
    delay(150);
    REGS_GPIO->pupd_enable = 0;
    REGS_GPIO->pupd_enable_clocks[pinNumber / 32] = 0;
}
