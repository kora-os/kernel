/* KoraOS init: the first and only program the kernel starts (loaded from
 * /bin/init). Following the classic Unix model, the kernel knows nothing about
 * what to run -- init owns that policy. For now it just launches the
 * interactive shell and waits for it; later it can read a boot configuration,
 * start background services, or bring up a GUI instead.
 */
#include "libk/koraos.h"

int main(void) {
    puts("init: starting /bin/shell");

    int pid = spawn("shell", 0, 0);
    if (pid < 0) {
        fputs("init: cannot start /bin/shell\n", stderr);
        return 1;
    }
    return wait(pid);
}
