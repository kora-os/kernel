/* ls: list a directory's entries. Usage: ls [path]  (defaults to "/").
 * Exercises open()/readdir()/close() against the FAT32 filesystem.
 */
#include "libk/koraos.h"

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "/";

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        kputs("ls: cannot open ");
        kputs(path);
        kputs("\n");
        return 1;
    }

    struct dirent de;
    int r;
    while ((r = readdir(fd, &de)) == 1) {
        kputs(de.name);
        if (de.is_dir) {
            kputs("/");
        }
        kputs("\n");
    }
    close(fd);
    return r < 0 ? 1 : 0;
}
