// SPDX-License-Identifier: GPL-3.0-or-later
//
// main() for libc_link_check (see user/libc/CMakeLists.txt): the whole libc
// archive, crt0's _start included, is linked behind it into a user-style PIE,
// so the link fails on any symbol the library needs but does not define.
// Never run.

int main(void) {
    return 0;
}
