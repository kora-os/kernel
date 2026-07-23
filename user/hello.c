/* First real KoraOS user program: a position-independent ELF (unlike the old
 * in-kernel hello.S). It prints a greeting and exits.
 *
 * `messages` is a global array of pointers held in writable data. Because it is
 * global (not foldable away) and read through a volatile index (so the load
 * survives -O2), each entry is materialized in memory, and in a PIE that means
 * each needs an R_AARCH64_RELATIVE relocation to hold the string's runtime
 * address. This makes the program a deliberate test of the Step 3 loader's
 * relocation pass: skip relocations and the pointer is wrong, so nothing (or
 * garbage) prints.
 */
#include "libk/koraos.h"

const char *messages[] = {
    "Hello from userland (ELF)\n",
};

static volatile int which = 0;

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

int main(void) {
    const char *msg = messages[which];
    write(1, msg, str_len(msg));
    return 0;
}
