/* newcon: a console on a screen of its own, running one program.
 *
 *   newcon program [args]
 *
 * Under the screen manager (screens), opens a screen with a console on it
 * (libscreen/console.h), runs the program with its stdin, stdout and stderr
 * there, and closes it when the program ends. The console edits the lines
 * the program reads (cooked mode) or hands it each key (raw mode, see
 * console_mode()); Ctrl+C kills the program. newcon reports how the program
 * ended on its own stdout. As AmigaOS's NewCon and CON:.
 */
#include "libk/koraos.h"
#include "libscreen/console.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: newcon program [args]\n", stderr);
        return 2;
    }
    char title[SCREEN_TITLE_MAX];
    snprintf(title, sizeof(title), "newcon %s", argv[1]);
    screen_t *screen = screen_open(title);
    if (screen == NULL) {
        fputs("newcon: no screen manager runs\n", stderr);
        return 1;
    }
    console_t *console = console_open(screen);
    if (console == NULL) {
        fputs("newcon: out of memory\n", stderr);
        screen_close(screen);
        return 1;
    }
    int code = -1;
    int ran = console_run(console, argv[1], argc - 1, argv + 1, CONSOLE_BREAK_KILL, &code);
    if (ran != 0) {
        printf("newcon: cannot run %s\n", argv[1]);
    } else {
        printf("newcon: %s exited with %d\n", argv[1], code);
    }
    console_close(console);
    screen_close(screen);
    return ran != 0;
}
