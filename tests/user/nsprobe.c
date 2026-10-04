// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 namespace regression fixture, including a child with independent cwd.
#include "../../user/libk/koraos.h"

static char initial[128], current[128], expected[128], program[160];

static void copy_text(char *destination, const char *source) {
    while ((*destination++ = *source++) != 0) {}
}
static void append_text(char *destination, const char *source) {
    copy_text(destination + kstrlen(destination), source);
}
static int fail(const char *message) {
    kputs("nsprobe: FAILED ");
    kputs(message);
    kputs("\n");
    return 1;
}

int main(int argc, char **argv) {
    if (argc == 3 && kstreq(argv[1], "child")) {
        if (getcwd(current, sizeof(current)) != 0 || !kstreq(current, argv[2])) {
            return fail("child did not inherit cwd");
        }
        if (chdir("/") != 0) return fail("child could not change cwd");
        kputs("nsprobe: child cwd checked\n");
        return 0;
    }
    if (getcwd(initial, sizeof(initial)) != 0 || initial[kstrlen(initial) - 1] != ':') {
        return fail("initial cwd is not a volume root");
    }
    int fd = open("/README.TXT", O_RDONLY);
    unsigned char before[64], after[64];
    ssize_t count = fd < 0 ? -1 : read(fd, before, sizeof(before));
    if (count <= 0) return fail("initial file read");
    if (chdir("docs") != 0) return fail("relative chdir");
    copy_text(expected, initial);
    append_text(expected, "docs");
    if (getcwd(current, sizeof(current)) != 0 || !kstreq(current, expected)) {
        return fail("canonical getcwd");
    }
    if (chdir("missing/..") != -1 || chdir("../README.TXT/..") != -1 ||
        getcwd(current, sizeof(current)) != 0 || !kstreq(current, expected)) {
        return fail("failed path changed cwd");
    }
    if (getcwd(current, 2) != -1) return fail("small getcwd buffer accepted");
    copy_text(program, initial);
    append_text(program, "bin/nsprobe");
    char *child_args[] = {"nsprobe", "child", expected};
    int child = spawn(program, 3, child_args);
    if (child < 0 || wait(child) != 0) return fail("child process");
    if (getcwd(current, sizeof(current)) != 0 || !kstreq(current, expected)) {
        return fail("child changed parent cwd");
    }
    if (argc > 1 && kstreq(argv[1], "extras")) {
        if (chdir("ExTrAs:DoCs") != 0 || getcwd(current, sizeof(current)) != 0 ||
            !kstreq(current, "extras:docs")) return fail("second partition label");
        if (chdir("/") != 0 || getcwd(current, sizeof(current)) != 0 ||
            !kstreq(current, "extras:")) return fail("current volume root");
        int other = open("/README.TXT", O_RDONLY);
        unsigned char marker[64];
        ssize_t marker_size = other < 0 ? -1 : read(other, marker, sizeof(marker));
        const char *prefix = "EXTRAS";
        if (marker_size < 6) return fail("second volume file");
        for (unsigned index = 0; index < 6; index++) {
            if (marker[index] != (unsigned char)prefix[index]) return fail("new fd selected wrong volume");
        }
        if (close(other) != 0) return fail("second volume file close");
        kputs("nsprobe: multi-volume checked\n");
    }
    if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, after, sizeof(after)) != count) {
        return fail("existing fd lost across chdir");
    }
    for (ssize_t index = 0; index < count; index++) {
        if (before[index] != after[index]) return fail("existing fd changed volume");
    }
    if (close(fd) != 0) return fail("file close");
    kputs("nsprobe: namespace checked\n");
    return 0;
}
