// SPDX-License-Identifier: GPL-3.0-or-later
//
// The scanf engine shared by sscanf (src/stdio/sscanf.c) and the stream
// functions (src/stdio/scanf.c). __scan() reads characters from `in`, one at
// a time, and never gives back more than the last one it took: that is all a
// stream guarantees (one ungetc), and all the standard's rules need.
#ifndef LIBC_SCAN_H
#define LIBC_SCAN_H

#include <stdarg.h>

struct scan_input {
    // The next character as an unsigned char, or EOF at the end of the input
    // (or on a read error), again and again once it is reached.
    int (*get)(struct scan_input *in);
    // Gives back c, the character get() returned last.
    void (*unget)(struct scan_input *in, int c);
};

// Returns what the scanf family returns: the number of items assigned, or
// EOF if the input ended before the first one was.
int __scan(struct scan_input *in, const char *format, va_list ap);

#endif // LIBC_SCAN_H
