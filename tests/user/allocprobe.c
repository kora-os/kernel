// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 regression fixture: allocate/touch/reap a heap, with a nested ELF child.
#include "../../user/libk/koraos.h"

int main(void) {
    const unsigned size = 64 * 1024;
    unsigned char *heap = sbrk(size);
    if (heap == (void *)-1) {
        kputs("allocprobe: allocation failed\n");
        return 1;
    }
    unsigned seed = (unsigned)getpid();
    for (unsigned i = 0; i < size; i++) {
        heap[i] = (unsigned char)(i * 37 + seed);
    }
    int child = spawn("hello", 0, 0);
    if (child < 0 || wait(child) != 0) {
        kputs("allocprobe: nested child failed\n");
        return 2;
    }
    for (unsigned i = 0; i < size; i++) {
        if (heap[i] != (unsigned char)(i * 37 + seed)) {
            kputs("allocprobe: heap corrupted\n");
            return 3;
        }
    }
    if (sbrk(-(long)size) == (void *)-1) {
        return 4;
    }
    kputs("allocprobe: heap checked\n");
    return 0;
}
