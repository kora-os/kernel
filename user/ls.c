/* ls: list a directory's entries. Usage: ls [path]  (defaults to the current directory).
 * Exercises open()/readdir()/close() against the FAT32 filesystem.
 */
#include "libk/koraos.h"

static struct dirent entry;  // 800 bytes: kept off the 4 KB stack

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "ls: cannot open %s\n", path);
        return 1;
    }

    int r;
    while ((r = readdir(fd, &entry)) == 1) {
        printf("%s%s\n", entry.name, entry.is_dir ? "/" : "");
    }
    close(fd);
    return r < 0 ? 1 : 0;
}
