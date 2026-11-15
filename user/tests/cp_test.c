// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of the actual cp utility (user/cp.c) on the fake kernel: short
// reads and writes under its file streams, an existing destination, a write
// the disk refuses, a failing sync, and sources that cannot be copied.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main cp_main
#include "../cp.c"
#undef main

static char source_bytes[7000];

static void reset(void) {
    fake_reset_files();
    fake_reset_console();
    for (unsigned i = 0; i < sizeof(source_bytes); i++) {
        source_bytes[i] = (char)(i * 19 + 7);
    }
    fake_put_file("source", source_bytes, sizeof(source_bytes));
}

// Runs cp and returns its exit status. main() returns here instead of going
// through exit(), so the streams are flushed by hand.
static int cp(int argc, char *source, char *destination) {
    char *argv[] = {"cp", source, destination, NULL};
    int status = cp_main(argc, argv);
    fflush(stdout);
    return status;
}

static int destination_is(const char *want, size_t want_len) {
    size_t len;
    const char *data = fake_file_data("destination", &len);
    return data != NULL && len == want_len && memcmp(data, want, len) == 0;
}

// Messages go to stderr; cp prints nothing on stdout.
static int said(const char *text) {
    return strcmp(fake_output(2, NULL), text) == 0 && strcmp(fake_output(1, NULL), "") == 0;
}

static void test_partial_io(void) {
    reset();
    fake_set_file_chunks(733, 37);
    CHECK(cp(3, "source", "destination") == 0, "cp completes short reads and writes");
    CHECK(destination_is(source_bytes, sizeof(source_bytes)), "the copied bytes match the source");
    CHECK(fake_open_fds() == 0 && fake_syncs() == 1, "both files closed, synced before success");
    CHECK(strcmp(fake_output(1, NULL), "") == 0 && strcmp(fake_output(2, NULL), "") == 0,
          "a successful copy prints nothing");
}

static void test_existing_destination(void) {
    reset();
    fake_put_file("destination", "keep me", 7);
    CHECK(cp(3, "source", "destination") == 1, "an existing destination is refused");
    CHECK(destination_is("keep me", 7), "and is neither truncated nor overwritten");
    CHECK(said("cp: cannot create new file destination\n"), "with a message");
    CHECK(fake_open_fds() == 0 && fake_syncs() == 1, "the source is closed, and cp still syncs");
}

static void test_write_and_sync_failures(void) {
    reset();
    fake_set_file_chunks(0, 37);
    fake_fail_file_writes_after(200);
    CHECK(cp(3, "source", "destination") == 1, "a refused write fails the copy");
    size_t len = 0;
    CHECK(fake_file_data("destination", &len) != NULL && len > 0 && len < sizeof(source_bytes),
          "the partial destination is left in place");
    CHECK(said("cp: copy failed; destination may be incomplete: destination\n"),
          "and the message says so");
    CHECK(fake_open_fds() == 0 && fake_syncs() == 1, "both files closed, synced after the failure");

    reset();
    fake_fail_sync(1);
    CHECK(cp(3, "source", "destination") == 1, "a failed sync is not reported as success");
    CHECK(destination_is(source_bytes, sizeof(source_bytes)) && said("cp: sync failed\n"),
          "although every byte was written");
}

static void test_bad_sources(void) {
    reset();
    CHECK(cp(3, "missing", "destination") == 1 && said("cp: cannot read file missing\n"),
          "a missing source");
    CHECK(fake_file_data("destination", NULL) == NULL, "creates no destination");
    reset();
    fake_mkdir("folder");
    CHECK(cp(3, "folder", "destination") == 1 && said("cp: cannot read file folder\n"),
          "a directory is not copied");
    reset();
    CHECK(cp(2, "source", NULL) == 1 && said("usage: cp source new-destination\n"),
          "usage with the wrong number of arguments");
    CHECK(fake_syncs() == 0, "which touches nothing");
}

TEST_MAIN(test_partial_io, test_existing_destination, test_write_and_sync_failures, test_bad_sources)
