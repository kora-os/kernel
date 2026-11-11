// SPDX-License-Identifier: GPL-3.0-or-later
//
// Entry point for libc_link_check (see user/libc/CMakeLists.txt): the whole
// libc archive is linked behind it into a user-style PIE, so the link fails on
// any symbol the library needs but does not define. Never run.

void _start(void) {
    for (;;) {
    }
}
