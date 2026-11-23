/* KoraOS init: the first and only program the kernel starts (loaded from
 * /bin/init). Following the classic Unix model, the kernel knows nothing about
 * what to run -- init owns that policy. With a display it runs the shell on a
 * screen of its own, under the screen manager (`screens shell`), and starts
 * it again when it ends. Without one there is no shell: the serial line's
 * kernel debug console is all there is.
 */
#include "libk/koraos.h"

// A shell that ends sooner than this is not started again: it cannot run.
#define QUICK_END_US 1000000

int main(void) {
    struct fb_info display;
    if (fb_info(&display) != 0 || display.bpp != 32) {
        puts("init: no display, so no shell; the serial line is the debug console");
        return 0;
    }
    char *argv[] = {"screens", "shell", NULL};
    for (;;) {
        puts("init: starting the shell (screens shell)");
        unsigned long start = uptime_us();
        int pid = spawn("screens", 2, argv);
        if (pid < 0) {
            fputs("init: cannot start /bin/screens\n", stderr);
            return 1;
        }
        int code = wait(pid);
        if (uptime_us() - start < QUICK_END_US) {
            printf("init: the shell ended at once (exit %d); not starting it again\n", code);
            return code;
        }
        printf("init: the shell ended (exit %d); starting it again\n", code);
    }
}
