// SPDX-License-Identifier: GPL-3.0-or-later
//
// rand and srand (C11 7.22.2): a 64-bit linear congruential generator (Knuth's
// MMIX constants) returning its top 31 bits, which are far better distributed
// than the low ones. Calling rand before srand behaves as srand(1). One state
// per program; rand is not required to be safe across threads.

#include <stdlib.h>

#define LCG_MULTIPLIER 6364136223846793005ull
#define LCG_INCREMENT 1442695040888963407ull

static unsigned long long state = 1;

int rand(void) {
    state = state * LCG_MULTIPLIER + LCG_INCREMENT;
    return (int)(state >> 33);  // 31 bits: 0 to RAND_MAX
}

void srand(unsigned int seed) {
    state = seed;
}
