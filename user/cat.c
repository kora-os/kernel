/* cat: print the contents of one or more files. Usage: cat <path>...
 * Exercises open()/read()/close() against the FAT32 filesystem.
 */
#include "libk/koraos.h"

static int cat_one(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        kputs("cat: cannot open ");
        kputs(path);
        kputs("\n");
        return 1;
    }

    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        write(1, buf, (size_t)n);
    }
    close(fd);
    return n < 0 ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        kputs("usage: cat <path>...\n");
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
