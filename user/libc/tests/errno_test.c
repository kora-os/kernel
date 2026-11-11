// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for <errno.h> (src/errno/errno.c) and strerror
// (src/string/strerror.c).

#include "test.h"

#include <errno.h>
#include <string.h>

static void test_errno_storage(void) {
    CHECK(__errno_location() == __errno_location(), "one errno location");
    errno = 0;
    CHECK(errno == 0, "errno starts writable");
    errno = ERANGE;
    CHECK(errno == ERANGE && *__errno_location() == ERANGE, "errno is the stored value");
    errno = 0;
}

static void test_codes_are_distinct(void) {
    static const int codes[] = {
        EPERM, ENOENT, EINTR, EIO, EBADF, ECHILD, EAGAIN, ENOMEM, EACCES, EFAULT, EBUSY,
        EEXIST, EXDEV, ENODEV, ENOTDIR, EISDIR, EINVAL, ENFILE, EMFILE, ENOTTY, EFBIG,
        ENOSPC, ESPIPE, EROFS, EPIPE, EDOM, ERANGE, ENAMETOOLONG, ENOSYS, ENOTEMPTY,
        EOVERFLOW, EILSEQ,
    };
    unsigned n = sizeof(codes) / sizeof(codes[0]);
    for (unsigned i = 0; i < n; i++) {
        CHECK(codes[i] > 0, "code %d is positive", codes[i]);
        for (unsigned j = i + 1; j < n; j++) {
            CHECK(codes[i] != codes[j], "codes %u and %u differ", i, j);
        }
        // Every code has its own message, not the fallback.
        CHECK(strcmp(strerror(codes[i]), "Unknown error") != 0, "strerror(%d) is known",
              codes[i]);
    }
}

static void test_strerror(void) {
    CHECK(strcmp(strerror(0), "No error") == 0, "strerror(0)");
    CHECK(strcmp(strerror(ENOENT), "No such file or directory") == 0, "strerror(ENOENT)");
    CHECK(strcmp(strerror(ERANGE), "Result out of range") == 0, "strerror(ERANGE)");
    CHECK(strcmp(strerror(EDOM), "Argument out of domain") == 0, "strerror(EDOM)");
    CHECK(strcmp(strerror(-1), "Unknown error") == 0, "strerror(-1)");
    CHECK(strcmp(strerror(9999), "Unknown error") == 0, "strerror(9999)");
    errno = 0;
    strerror(12345);
    CHECK(errno == 0, "strerror does not touch errno");
}

TEST_MAIN(test_errno_storage, test_codes_are_distinct, test_strerror)
