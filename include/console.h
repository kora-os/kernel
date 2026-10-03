#pragma once

#include "common.h"

// Kernel debug console on the UART ("koraos> "). It is not a task: it is fed one
// byte at a time by the serial input router (tty.c), from the UART interrupt or
// from the terminal's idle loop, so it runs alongside the shell on the screen.
// Commands execute in that context: they must not block, and may use the kernel
// heap (it is interrupt safe) only briefly.

#define CONSOLE_MAX_CMD_LEN 64

// Command handler function type
typedef void (*console_cmd_handler_t)(const char *args);

// Command structure
typedef struct {
    const char *name;
    const char *description;
    console_cmd_handler_t handler;
} console_command_t;

// Print the banner (the serial router then prints the prompt if it routes the
// UART here).
void console_init(void);

// Print the prompt again, with any partially typed line (after other output).
void console_prompt(void);

// Feed one received byte: line editing, and command execution on Enter.
void console_input(char c);

// Print a log message (for system logging)
void console_log(const char *message);
