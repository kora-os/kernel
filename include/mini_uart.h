#pragma once

#include "arch/irq.h"
#include "common.h"

void uart_init(void);
void uart_putc(unsigned char c);
unsigned char uart_getc(void);
// Nonzero if a received byte is waiting (uart_getc() would not block).
int uart_rx_ready(void);

void uart_puts(const char *str);

// Route the UART's receive interrupt to `handler`, which must drain the RX FIFO
// (reading the data clears the interrupt).
void uart_rx_irq_enable(irq_handler_t handler);
