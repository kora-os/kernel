/* KoraOS init: the first user program the kernel starts. It spawns two
 * instances of hello to demonstrate the process model.
 *
 * Because a finished task lingers as a zombie (its memory held until reaped),
 * the second spawn cannot reuse the first instance's region -- so the kernel's
 * per-task launch log shows the two hello instances at two different entry and
 * stack addresses, proving they are independently loaded.
 */
#include "libk/koraos.h"

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void puts_(const char *s) {
    write(1, s, str_len(s));
}

int main(void) {
    puts_("init: launching two hello instances\n");
    spawn("hello");
    spawn("hello");
    puts_("init: done\n");
    return 0;
}
