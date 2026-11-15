// Copy one regular file to a new destination. Existing files are preserved.
#include "libk/filetools.h"

static unsigned char buffer[4096];

int main(int argc, char **argv) {
    if (argc != 3) {
        fputs("usage: cp source new-destination\n", stderr);
        return 1;
    }
    struct stat info;
    if (stat(argv[1], &info) < 0 || info.is_dir) {
        filetool_error("cp", "cannot read file", argv[1]);
        return filetool_finish("cp", 1);
    }
    FILE *source = fopen(argv[1], "rb");
    if (source == NULL) {
        filetool_error("cp", "cannot open", argv[1]);
        return filetool_finish("cp", 1);
    }
    // "x": create the destination, or fail if it exists.
    FILE *destination = fopen(argv[2], "wbx");
    if (destination == NULL) {
        fclose(source);
        filetool_error("cp", "cannot create new file", argv[2]);
        return filetool_finish("cp", 1);
    }
    int failed = 0;
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        if (fwrite(buffer, 1, count, destination) != count) {
            failed = 1;
            break;
        }
    }
    if (ferror(source)) failed = 1;
    if (fclose(source) == EOF) failed = 1;
    // The last buffered bytes are written here, so a full disk can show up now.
    if (fclose(destination) == EOF) failed = 1;
    if (failed) {
        filetool_error("cp", "copy failed; destination may be incomplete:", argv[2]);
    }
    return filetool_finish("cp", failed);
}
