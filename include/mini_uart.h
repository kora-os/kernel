#pragma once

#include "common.h"

void uart_init(void);
void uart_putc(unsigned char c);
unsigned char uart_getc(void);
// Nonzero if a received byte is waiting (uart_getc() would not block).
int uart_rx_ready(void);

void uart_puts(const char *str);
int uart_readline(char *buffer, int max_len);
