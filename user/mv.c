#include "libk/filetools.h"

int main(int argc, char **argv) {
    if (argc != 3) {
        kputs("usage: mv source new-destination\n");
        return 1;
    }
    int failed = rename(argv[1], argv[2]) < 0;
    if (failed) filetool_error("mv", "cannot rename", argv[1]);
    return filetool_finish("mv", failed);
}
