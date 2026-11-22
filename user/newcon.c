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

static int program = -1;

static void kill_program(void *ctx) {
    (void)ctx;
    if (program > 0) {
        kill(program);
    }
}

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
    int end_bit = sig_alloc(-1);
    if (console == NULL || end_bit < 0) {
        fputs("newcon: out of memory\n", stderr);
        screen_close(screen);
        return 1;
    }
    struct std_handle std[3];
    for (int i = 0; i < 3; i++) {
        std[i] = (struct std_handle){STD_PORT, console_port(console)};
    }
    program = spawn_std(argv[1], argc - 1, argv + 1, SPAWN_NOWAIT | SPAWN_SIGNAL(end_bit), std);
    int code = -1;
    if (program < 0) {
        printf("newcon: cannot run %s\n", argv[1]);
    } else {
        console_on_break(console, kill_program, NULL);
        for (;;) {
            unsigned got = sig_wait(console_signals(console) | SIG_MASK(end_bit));
            console_service(console, got);
            if ((got & SIG_MASK(end_bit)) && waitpid(program, &code, WNOHANG) == program) {
                break;
            }
        }
        printf("newcon: %s exited with %d\n", argv[1], code);
    }
    console_close(console);
    screen_close(screen);
    sig_free(end_bit);
    return program < 0 ? 1 : 0;
}
