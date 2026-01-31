#include "mini_uart.h"
#include "peripherals/gpio.h"
#include "utils.h"

#ifdef QEMU_TESTING
// Use PL011 UART for QEMU which has better support
#include "peripherals/pl011.h"

void uart_init(void) {
    // Disable UART
    REGS_PL011->cr = 0;
    
    // Wait for end of transmission
    while (REGS_PL011->fr & (1 << 3)) { }
    
    // Configure GPIO pins 14 and 15 for PL011 UART (Alt0)
    uint32_t selector = REGS_GPIO->func_select[1];
    selector &= ~((7 << 12) | (7 << 15));  // Clear pins 14 and 15
    selector |= (4 << 12) | (4 << 15);     // Set to Alt0 (PL011 UART)
    REGS_GPIO->func_select[1] = selector;
    
    // Disable pull-up/pull-down for pins 14 and 15
    REGS_GPIO->pupd_enable = 0;
    delay(150);
    REGS_GPIO->pupd_enable_clocks[0] = (1 << 14) | (1 << 15);
    delay(150);
    REGS_GPIO->pupd_enable_clocks[0] = 0;
    
    // Clear pending interrupts
    REGS_PL011->icr = 0x7FF;
    
    // Set baud rate to 115200
    // UART clock = 3MHz, baud = 115200
    // Divisor = 3000000 / (16 * 115200) = 1.627
    // Integer part = 1, Fractional part = 0.627 * 64 = 40
    REGS_PL011->ibrd = 1;
    REGS_PL011->fbrd = 40;
    
    // Enable FIFO, 8-bit data, 1 stop bit, no parity
    REGS_PL011->lcrh = (1 << 4) | (3 << 5);
    
    // Mask all interrupts
    REGS_PL011->imsc = 0;
    
    // Enable UART, transmit, and receive
    REGS_PL011->cr = (1 << 0) | (1 << 8) | (1 << 9);
}

void uart_putc(unsigned char c) {
    // Wait until transmit FIFO is not full (FR bit 5)
    while (REGS_PL011->fr & (1 << 5)) {
        // Wait
    }
    
    // Write the character
    REGS_PL011->dr = (uint32_t)c;
}

unsigned char uart_getc(void) {
    // Wait until receive FIFO is not empty (FR bit 4)
    while (REGS_PL011->fr & (1 << 4)) {
        // Wait
    }
    
    // Read and return the character
    return (unsigned char)(REGS_PL011->dr & 0xFF);
}

#else
// Use Mini UART for real hardware
#include "peripherals/aux.h"

void uart_init(void) {
    // Enable mini UART (this also enables access to its registers)
    REGS_AUX->enables = 1;
    
    // Disable transmitter and receiver during configuration
    REGS_AUX->mu_control = 0;
    
    // Disable interrupts
    REGS_AUX->mu_ier = 0;
    
    // Clear FIFOs
    REGS_AUX->mu_iir = 0xC6;
    
    // Set 8-bit mode
    REGS_AUX->mu_lcr = 0x03;
    
    // Set baud rate to 115200
    // Baud rate = system_clock / (8 * (baudrate_reg + 1))
    // For 250MHz system clock: 250000000 / (8 * 115200) = 270
#if RPI_VERSION == 4
    REGS_AUX->mu_baud_rate = 541;   // 500 MHz core / 115200 baud
#elif RPI_VERSION == 3
    REGS_AUX->mu_baud_rate = 270;   // 250 MHz core / 115200 baud
#else
    REGS_AUX->mu_baud_rate = 270;
#endif    
    // Configure GPIO pins 14 and 15 for Mini UART (Alt5)
    uint32_t selector = REGS_GPIO->func_select[1];
    selector &= ~((7 << 12) | (7 << 15));  // Clear pins 14 and 15
    selector |= (2 << 12) | (2 << 15);     // Set to Alt5 (Mini UART)
    REGS_GPIO->func_select[1] = selector;
    
    // Disable pull-up/pull-down for pins 14 and 15
    REGS_GPIO->pupd_enable = 0;
    delay(150);
    REGS_GPIO->pupd_enable_clocks[0] = (1 << 14) | (1 << 15);
    delay(150);
    REGS_GPIO->pupd_enable_clocks[0] = 0;
    
    // Enable transmitter and receiver
    REGS_AUX->mu_control = 0x03;
}

void uart_putc(unsigned char c) {
    // Wait until transmitter is ready (LSR bit 5 = transmitter empty)
    while (!(REGS_AUX->mu_lsr & (1 << 5))) {
        // Wait
    }
    
    // Write the character
    REGS_AUX->mu_io = (uint32_t)c;
}

unsigned char uart_getc(void) {
    // Wait until data is available (LSR bit 0 = data ready)
    while (!(REGS_AUX->mu_lsr & (1 << 0))) {
        // Wait
    }
    
    // Read and return the character
    return (unsigned char)(REGS_AUX->mu_io & 0xFF);
}
#endif

// Hardware-agnostic helper function
void uart_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            uart_putc('\r');
        }
        uart_putc(*str++);
    }
}

int uart_readline(char *buffer, int max_len) {
    int pos = 0;
    
    while (1) {
        unsigned char c = uart_getc();
        
        // Handle carriage return or newline
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            buffer[pos] = '\0';
            return pos;
        }
        
        // Handle backspace (ASCII 8 or 127)
        if (c == 8 || c == 127) {
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");  // Move back, space, move back again
            }
            continue;
        }
        
        // Handle printable characters
        if (c >= 32 && c < 127 && pos < max_len - 1) {
            buffer[pos++] = c;
            uart_putc(c);  // Echo character
        }
    }
}
