// SPDX-License-Identifier: GPL-3.0-or-later
//
// koracli: KoraCLI's console task (project_doc/koracli-spec.md §3.6). It
// opens a screen with a console on it and runs the interpreter there,
// MicroPython with KoraCLI's Python side (sys:koracli/main.py): the editor,
// the bars and the dispatcher are that, and this program serves the console
// they run on (its grid, its bars, its scrollback), so the screen stays
// responsive while Python runs. Ctrl+C on the console is a break for the
// interpreter (KeyboardInterrupt); it ends when the interpreter does.
//
// Usage: koracli
#include "libk/koraos.h"
#include "libscreen/console.h"
#include "libscreen/screen.h"

#define KORACLI_MAIN "sys:koracli/main.py"

int main(void) {
    screen_t *screen = screen_open("koracli");
    if (screen == NULL) {
        fputs("koracli: no screen manager runs (start one with: screens koracli)\n", stderr);
        return 1;
    }
    console_t *console = console_open(screen);
    if (console == NULL) {
        fputs("koracli: out of memory\n", stderr);
        screen_close(screen);
        return 1;
    }
    char *argv[] = {"micropython", KORACLI_MAIN, NULL};
    int code = 1;
    if (console_run(console, "micropython", 2, argv, CONSOLE_BREAK_SIGNAL, &code) != 0) {
        fputs("koracli: cannot start micropython on its console\n", stderr);
    }
    console_close(console);
    screen_close(screen);
    return code;
}
