/* KoraOS init: the first and only program the kernel starts (loaded from
 * /bin/init). Following the classic Unix model, the kernel knows nothing about
 * what to run -- init owns that policy. With a display it runs the shell,
 * KoraCLI, on a screen of its own, under the screen manager (`screens
 * koracli`), and starts it again when it ends (unless it keeps ending at
 * once). Without one there is no shell: the serial line's kernel debug
 * console is all there is.
 */
#include "libk/koraos.h"

// A shell that ends this soon QUICK_ENDS times in a row cannot run: init
// stops starting it. (One quick exit is only someone typing fast.)
#define QUICK_END_US 1000000
#define QUICK_ENDS 3

int main(void) {
    struct fb_info display;
    if (fb_info(&display) != 0 || display.bpp != 32) {
        puts("init: no display, so no shell; the serial line is the debug console");
        return 0;
    }
    char *argv[] = {"screens", "koracli", NULL};
    int quick = 0;
    for (;;) {
        puts("init: starting the shell (screens koracli)");
        unsigned long start = uptime_us();
        int pid = spawn("screens", 2, argv);
        if (pid < 0) {
            fputs("init: cannot start /bin/screens\n", stderr);
            return 1;
        }
        int code = wait(pid);
        quick = uptime_us() - start < QUICK_END_US ? quick + 1 : 0;
        if (quick == QUICK_ENDS) {
            printf("init: the shell ended at once %d times (exit %d); not starting it again\n",
                   QUICK_ENDS, code);
            return code;
        }
        printf("init: the shell ended (exit %d); starting it again\n", code);
    }
}
