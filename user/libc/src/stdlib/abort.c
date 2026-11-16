// SPDX-License-Identifier: GPL-3.0-or-later
//
// abort() (C11 7.22.4.1): raises SIGABRT, then ends the program at once even
// if a handler caught the signal and returned, or the signal is ignored. Only
// a handler that does not return (it calls longjmp or exit) avoids the end.
// No stream is flushed and no atexit handler runs. In a file of its own, so
// that only programs calling abort() link the signal table.

#include <signal.h>
#include <stdlib.h>

void abort(void) {
    raise(SIGABRT);
    _Exit(-SIGABRT);
}
