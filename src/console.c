#include "console.h"
#include "arch/irq.h"
#include "arch/systick.h"
#include "common.h"
#include "lib/timer.h"
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
}

void console_cmd_help(const char *args) {
  uart_puts("Available commands:\n");
  uart_puts("  help - Show available commands\n");
  uart_puts("  get_el - Get current Exception Level\n");
  uart_puts("  version - Print current KoraOS version\n");
  uart_puts("  irqs - Show interrupt counters and the system tick\n");
  uart_puts("Ctrl-T switches the serial line between this console and the\n");
  uart_puts("screen terminal (the shell).\n");
}

void console_cmd_get_el(const char *args) {
  printf("Current Exception Level: %d\n", get_el());
}

void console_cmd_version(const char *args) {
  printf("KoraOS version %s\n", KORAOS_VERSION);
}

// Interrupt health at a glance: the tick count should track the uptime (100 per
// second), and every connected IRQ is listed with how often it fired.
void console_cmd_irqs(const char *args) {
  uint64_t ms = timer_us() / 1000;
  printf("controller: %s\n", irq_controller());
  printf("systick: %lu ticks, uptime %lu.%03lu s\n",
         (unsigned long)systick_count(), (unsigned long)(ms / 1000),
         (unsigned long)(ms % 1000));
  printf("  IRQ        hits  source\n");
  for (unsigned irq = 0; irq < irq_lines(); irq++) {
    unsigned long hits = irq_hits(irq);
    const char *name = irq_name(irq);
    if (hits == 0 && name[0] == '\0') {
      continue;
    }
    printf("  %3u  %10lu  %s\n", irq, hits, name[0] ? name : "(no handler)");
  }
}

console_command_t commands[] = {
    {"help", "Show available commands", console_cmd_help},
    {"get_el", "Get the current Exception Level", console_cmd_get_el},
    {"version", "Get current KoraOS version", console_cmd_version},
    {"irqs", "Show interrupt counters and the system tick", console_cmd_irqs},
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

static char line[CONSOLE_MAX_CMD_LEN];
static int line_len;
static bool last_was_cr;

void console_prompt(void) {
  uart_puts("koraos> ");
  for (int i = 0; i < line_len; i++) {
    uart_putc(line[i]);
  }
}

void console_input(char c) {
  // Serial terminals send CR, LF or CR LF for Enter: treat the LF of a CR LF
  // pair as part of the same keypress.
  if (c == '\n' && last_was_cr) {
    last_was_cr = false;
    return;
  }
  last_was_cr = (c == '\r');

  if (c == '\r' || c == '\n') {
    uart_puts("\n");
    line[line_len] = '\0';
    if (line_len > 0) {
      console_parse_and_execute(line);
    }
    line_len = 0;
    console_prompt();
    return;
  }
  if (c == 8 || c == 127) {  // backspace / delete
    if (line_len > 0) {
      line_len--;
      uart_puts("\b \b");
    }
    return;
  }
  if (c >= 32 && c < 127 && line_len < CONSOLE_MAX_CMD_LEN - 1) {
    line[line_len++] = c;
    uart_putc(c);
  }
}

void console_log(const char *message) {
  uart_puts("[LOG] ");
  uart_puts(message);
  uart_puts("\n");
}
