/* echo: print the arguments passed by the shell, space-separated. The simplest
 * proof that argv reaches a spawned program.
 */
#include "libk/koraos.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            write(1, " ", 1);
        }
        write(1, argv[i], kstrlen(argv[i]));
    }
    write(1, "\n", 1);
    return argc - 1;  // exit code = number of arguments echoed
}
