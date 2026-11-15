/* echo: print the arguments passed by the shell, space-separated. The simplest
 * proof that argv reaches a spawned program.
 */
#include "libk/koraos.h"

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) {
            putchar(' ');
        }
        fputs(argv[i], stdout);
    }
    putchar('\n');
    return argc - 1;  // exit code = number of arguments echoed
}
