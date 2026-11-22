/* cat: print the contents of one or more files. Usage: cat [path...]
 * Reads through the libc's file streams, on the FAT32 filesystem. With no
 * path it copies stdin, when stdin can end: a file (cat < notes.txt) or a
 * console with line editing (newcon), where Ctrl+D ends the input. The
 * kernel console has no end of input, so there it prints its usage instead.
 */
#include "libk/koraos.h"

// Copy `file` to stdout; nonzero if it could not be read.
static int copy(FILE *file) {
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    return ferror(file);
}

static int cat_one(const char *path) {
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        fflush(stdout);  // what the files before it printed comes first
        fprintf(stderr, "cat: cannot open %s\n", path);
        return 1;
    }

    int failed = copy(file);
    fclose(file);
    if (failed) {  // a directory opens, but cannot be read
        fflush(stdout);
        fprintf(stderr, "cat: cannot read %s\n", path);
    }
    return failed ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        // stdin can end if it is a file, or a console that has a cooked
        // mode (and with it Ctrl+D); not the kernel console.
        if (isatty(0) && console_mode(CON_COOKED) != 0) {
            fputs("usage: cat [path...] (with no path, stdin: a file or a console window)\n", stderr);
            return 1;
        }
        // A byte at a time: stdin hands over what is there (a console, a
        // line), and fread would wait to fill its buffer.
        int c;
        while ((c = getc(stdin)) != EOF) {
            putc(c, stdout);
        }
        if (ferror(stdin)) {
            fflush(stdout);
            fputs("cat: cannot read stdin\n", stderr);
            return 1;
        }
        return 0;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (cat_one(argv[i]) != 0) {
            rc = 1;
        }
    }
    return rc;
}
