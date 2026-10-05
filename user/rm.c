#include "libk/filetools.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        kputs("usage: rm file...\n");
        return 1;
    }
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (unlink(argv[i]) < 0) {
            filetool_error("rm", "cannot remove", argv[i]);
            failed = 1;
        }
    }
    return filetool_finish("rm", failed);
}
