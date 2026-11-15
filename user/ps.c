/* ps: list the running tasks (every thread of every program), with the core
 * each one runs on. TID is the thread; it equals PID for a program's main
 * thread. */
#include "libk/koraos.h"

int main(void) {
    static const char *const states[] = {"running ", "ready   ", "sleeping", "zombie  "};
    struct task_info info;
    puts("  PID   TID  PPID  CPU  STATE     NAME");
    for (unsigned i = 0; task_info(i, &info) == 1; i++) {
        printf("%5d%6d%6d%5u  %s  %s\n", info.pid, info.tid, info.ppid, info.core,
               info.state <= TASK_ZOMBIE ? states[info.state] : "?       ", info.name);
    }
    return 0;
}
