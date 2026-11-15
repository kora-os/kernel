// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture for lazy FP/SIMD context switching: a parent fills
// all 32 vector registers and FPCR, spawns a child that must start from zeroed
// registers and fills them with its own values, and then checks that its own
// values survived. Repeated, so ownership changes in both directions.
#include "../../user/libk/koraos.h"

// No libc code may run while the registers hold a pattern, or before the
// child has looked at the ones it started with: the libc is free to use
// them. So the probe compares and prints with its own few lines, straight to
// the console, and starts its child with the raw spawn stub.
static int same(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void say(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    write(1, s, n);
}

void fp_fill(unsigned long seed);
unsigned long fp_check(unsigned long seed);
unsigned long fp_nonzero(void);

static int child(void) {
    if (fp_nonzero() != 0) {
        say("fpprobe: child saw another task's FP registers\n");
        return 3;
    }
    fp_fill(0x2000);
    yield();
    if (fp_check(0x2000) != 0) {
        say("fpprobe: child FP registers changed\n");
        return 4;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 1 && same(argv[1], "child")) {
        return child();
    }
    char *args[] = {"fpprobe", "child", NULL};
    for (unsigned round = 0; round < 3; round++) {
        unsigned long seed = 0x1000 + round * 0x100;
        fp_fill(seed);
        // The raw stub: spawn() flushes stdout first, which is libc code.
        int pid = libk_spawn_flags("fpprobe", 2, args, 0);
        int code = pid < 0 ? -1 : wait(pid);
        if (code != 0) {
            say("fpprobe: child failed\n");
            return 1;
        }
        if (fp_check(seed) != 0) {
            say("fpprobe: parent FP registers lost across the child\n");
            return 2;
        }
    }
    say("fpprobe: registers preserved\n");
    return 0;
}
