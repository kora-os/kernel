// SPDX-License-Identifier: GPL-3.0-or-later
//
// fake_reset_std_streams (fake_syscall.h), apart from the fake kernel: it
// needs the stdio internals, which not every test that uses the fake kernel
// links.

#include "fake_syscall.h"
#include "stdio_impl.h"

void fake_reset_std_streams(void) {
    for (int i = 0; i < 3; i++) {
        FILE *f = __stdio_standard(i);
        fflush(f);
        clearerr(f);
        f->fd = i;
        f->port = 0;
        f->reply = 0;
        f->rpos = f->rlen = 0;
        f->ungot = EOF;
        f->mode = i == 2 ? _IONBF : _IOLBF;
    }
    __stdio_std_ready = 0;
}
