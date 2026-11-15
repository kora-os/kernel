// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: checksum files without printing them.
//   sumprobe <path>...   prints "sumprobe: <path> <size> <fnv1a64 in hex>" per
//                        file (one write each), exit 1 if any cannot be read
// The smoke test compares against the host's copy of the same bytes.
#include "../../user/libk/koraos.h"

int main(int argc, char **argv) {
    static unsigned char buf[8192];
    int failed = 0;
    for (int a = 1; a < argc; a++) {
        int fd = open(argv[a], O_RDONLY);
        unsigned long hash = 0xcbf29ce484222325ul;
        unsigned long size = 0;
        long n = fd < 0 ? -1 : 0;
        while (fd >= 0 && (n = read(fd, buf, sizeof(buf))) > 0) {
            for (long i = 0; i < n; i++) {
                hash = (hash ^ buf[i]) * 0x100000001b3ul;
            }
            size += (unsigned long)n;
        }
        if (fd >= 0) {
            close(fd);
        }
        // One line per file: stdout writes it whole at the newline.
        if (n < 0) {
            printf("sumprobe: %s unreadable\n", argv[a]);
            failed = 1;
        } else {
            printf("sumprobe: %s %lu %016lx\n", argv[a], size, hash);
        }
    }
    return failed;
}
