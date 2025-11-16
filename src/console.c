#include "console.h"
#include "common.h"
#include "lib/printf.h"
#include "lib/string.h"
#include "mini_uart.h"
#include "utils.h"

void console_init(void) {

  // Print welcome banner
  uart_puts("\n");
  uart_puts("======================================\n");
  uart_puts("   KoraOS Debug Console\n");
  uart_puts("======================================\n");
  uart_puts("Type 'help' for available commands.\n");
  uart_puts("\n");
}

void console_cmd_help(const char *args) {
  uart_puts("Available commands:\n");
  uart_puts("  help - Show available commands\n");
  uart_puts("  exit - Exit the console\n");
  uart_puts("  get_el - Get current Exception Level\n");
  uart_puts("  version - Print current KoraOS version\n");
}
void console_cmd_exit(const char *args) {
  uart_puts("Exiting console...\n");

#ifdef QEMU_TESTING
  uart_puts("...well, press CTRL-A X to quit QEMU.\n");
#else
  uart_puts("...Power down the Raspberry Pi.\n");
#endif
}

void console_cmd_get_el(const char *args) {
  printf("Current Exception Level: %d\n", get_el());
}

void console_cmd_version(const char *args) {
  printf("KoraOS version %s\n", KORAOS_VERSION);
}

console_command_t commands[] = {
    {"help", "Show available commands", console_cmd_help},
    {"exit", "Exit the console", console_cmd_exit},
    {"get_el", "Get the current Exception Level", console_cmd_get_el},
    {"version", "Get current KoraOS version", console_cmd_version},
    {NULL, NULL, NULL},
};

void console_parse_and_execute(const char *input) {
  for (int i = 0; commands[i].name != NULL; i++) {
    if (strcmp(input, commands[i].name) == 0) {
      commands[i].handler(input);
      return;
    }
  }

  uart_puts("Unknown command: '");
  uart_puts(input);
  uart_puts("'\nType 'help' for available commands.\n\n");
}

void console_run(void) {
  char input_buffer[CONSOLE_MAX_CMD_LEN];

  while (1) {
    uart_puts("koraos> ");
    uart_readline(input_buffer, CONSOLE_MAX_CMD_LEN);
    console_parse_and_execute(input_buffer);
  }
}

void console_log(const char *message) {
  uart_puts("[LOG] ");
  uart_puts(message);
  uart_puts("\n");
}
