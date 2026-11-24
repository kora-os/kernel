/* font: the font of the console it writes to.
 *
 *   font            lists the fonts in sys:fonts, and builtin
 *   font NAME       draws the console in sys:fonts/NAME.psf (or in the PSF2
 *                   file NAME, if it is a path: it has a '/' or a ':')
 *   font builtin    back to the built-in 8x8 font
 *
 * The console resizes to the cells the font gives (console_font in libk);
 * its text stays. A program that asks the console for its size (CSI 18 t)
 * gets the new one.
 */
#include "libk/koraos.h"

#define FONT_DIR "sys:fonts"
#define FONT_SUFFIX ".psf"
#define FONT_MAX (1024 * 1024)  /* larger than any console font */

/* Kept off the stack, which is a single 4 KB page for a program's main
 * thread: a path alone is larger. */
static struct dirent font_entry;
static char path[KORA_PATH_MAX];

static int has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    return n > m && strcmp(s + n - m, suffix) == 0;
}

static int list_fonts(void) {
    int fd = open(FONT_DIR, O_RDONLY);
    if (fd < 0) {
        fputs("font: no " FONT_DIR "\n", stderr);
        return 1;
    }
    int r;
    while ((r = readdir(fd, &font_entry)) == 1) {
        if (!font_entry.is_dir && has_suffix(font_entry.name, FONT_SUFFIX)) {
            printf("%.*s\n", (int)(strlen(font_entry.name) - strlen(FONT_SUFFIX)), font_entry.name);
        }
    }
    close(fd);
    puts("builtin");
    return r < 0 ? 1 : 0;
}

/* The whole file at `path` in memory, its size in *size; NULL if it cannot
 * be read, or is larger than any font. */
static void *read_font(const char *path, size_t *size) {
    struct stat st;
    if (stat(path, &st) != 0 || st.is_dir || st.size == 0 || st.size > FONT_MAX) {
        return NULL;
    }
    FILE *file = fopen(path, "r");
    char *data = malloc(st.size);
    if (file == NULL || data == NULL) {
        if (file != NULL) {
            fclose(file);
        }
        free(data);
        return NULL;
    }
    size_t n = fread(data, 1, st.size, file);
    fclose(file);
    if (n != st.size) {
        free(data);
        return NULL;
    }
    *size = n;
    return data;
}

int main(int argc, char **argv) {
    if (argc == 1) {
        return list_fonts();
    }
    if (argc != 2) {
        fputs("usage: font [NAME | path | builtin]\n", stderr);
        return 2;
    }
    const char *name = argv[1];
    if (strcmp(name, "builtin") == 0) {
        if (console_font(NULL, 0) != 0) {
            fputs("font: this console has no fonts\n", stderr);
            return 1;
        }
        return 0;
    }
    if (strchr(name, '/') != NULL || strchr(name, ':') != NULL) {
        snprintf(path, sizeof(path), "%s", name);
    } else {
        snprintf(path, sizeof(path), "%s/%s%s", FONT_DIR, name, FONT_SUFFIX);
    }
    size_t size = 0;
    void *data = read_font(path, &size);
    if (data == NULL) {
        fprintf(stderr, "font: cannot read %s\n", path);
        return 1;
    }
    int result = console_font(data, size);
    free(data);
    if (result != 0) {
        fprintf(stderr, "font: cannot use %s here: not a PSF2 font, too large, or not on a "
                        "console with fonts\n", path);
        return 1;
    }
    return 0;
}
