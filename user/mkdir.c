#include "libk/filetools.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        kputs("usage: mkdir directory...\n");
        return 1;
    }
    int failed = 0;
    for (int i = 1; i < argc; i++) {
        if (mkdir(argv[i]) < 0) {
            filetool_error("mkdir", "cannot create", argv[i]);
            failed = 1;
        }
    }
    return filetool_finish("mkdir", failed);
}
