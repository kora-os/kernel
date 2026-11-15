/* cat: print the contents of one or more files. Usage: cat <path>...
 * Reads through the libc's file streams, on the FAT32 filesystem.
 */
#include "libk/koraos.h"

static int cat_one(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fflush(stdout);  // what the files before it printed comes first
        fprintf(stderr, "cat: cannot open %s\n", path);
        return 1;
    }

    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) {  // a directory opens, but cannot be read
        fflush(stdout);
        fprintf(stderr, "cat: cannot read %s\n", path);
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: cat <path>...\n", stderr);
        return 1;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (cat_one(argv[i]) != 0) {
            rc = 1;
        }
    }
    return rc;
}
