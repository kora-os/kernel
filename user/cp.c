// Copy one regular file to a new destination. Existing files are preserved.
#include "libk/filetools.h"

static unsigned char buffer[4096];

int main(int argc, char **argv) {
    if (argc != 3) {
        kputs("usage: cp source new-destination\n");
        return 1;
    }
    struct stat info;
    if (stat(argv[1], &info) < 0 || info.is_dir) {
        filetool_error("cp", "cannot read file", argv[1]);
        return filetool_finish("cp", 1);
    }
    int source = open(argv[1], O_RDONLY);
    if (source < 0) {
        filetool_error("cp", "cannot open", argv[1]);
        return filetool_finish("cp", 1);
    }
    int destination = open(argv[2], O_WRONLY | O_CREAT | O_EXCL);
    if (destination < 0) {
        close(source);
        filetool_error("cp", "cannot create new file", argv[2]);
        return filetool_finish("cp", 1);
    }
    int failed = 0;
    ssize_t count;
    while ((count = read(source, buffer, sizeof(buffer))) > 0) {
        ssize_t position = 0;
        while (position < count) {
            ssize_t written = write(destination, buffer + position, (size_t)(count - position));
            if (written <= 0) {
                failed = 1;
                break;
            }
            position += written;
        }
        if (failed) break;
    }
    if (count < 0) failed = 1;
    if (close(source) < 0) failed = 1;
    if (close(destination) < 0) failed = 1;
    if (failed) {
        filetool_error("cp", "copy failed; destination may be incomplete:", argv[2]);
    }
    return filetool_finish("cp", failed);
}
