#include "console.h"
#include "lib/printf.h"
#include "lib/stdlib.h"
#include "lib/string.h"
#include "mini_uart.h"
#include "utils.h"

void putc(void *p, char c) {
  if (c == '\n') {
    uart_putc('\r');
  }

  uart_putc(c);
}

void kernel_main(void) {
  uart_init();
  uart_putc('K');
  uart_putc('\n');

  init_printf(NULL, putc);

#if RPI_VERSION == 4
#if QEMU_TESTING
  console_log("KoraOS is running on a Raspberry Pi 4 in QEMU!\n");
#else
  console_log("KoraOS is running on a Raspberry Pi 4!\n");
#endif
#else
#if QEMU_TESTING
  console_log("KoraOS is running on a Raspberry Pi 3 in QEMU!\n");
#else
  console_log("KoraOS is running on a Raspberry Pi 3!\n");
#endif
#endif

  printf("Current EL: %d\n", get_el());
  console_init();
  console_run();
}
