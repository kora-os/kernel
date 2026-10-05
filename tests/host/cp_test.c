// SPDX-License-Identifier: GPL-3.0-or-later
// Test the actual utility with partial I/O and persistence failures.
#include "test.h"
#define main cp_test_main
#define stat(...) cp_mock_stat(__VA_ARGS__)
#define open(...) cp_mock_open(__VA_ARGS__)
#define read(...) cp_mock_read(__VA_ARGS__)
#define write(...) cp_mock_write(__VA_ARGS__)
#define close(...) cp_mock_close(__VA_ARGS__)
#define sync(...) cp_mock_sync(__VA_ARGS__)
#include "../../user/cp.c"
#undef main
#undef stat
#undef open
#undef read
#undef write
#undef close
#undef sync

static unsigned char source_bytes[7000], destination_bytes[7000];
static unsigned destination_size, source_position, close_count, sync_count;
static bool reject_existing, fail_write, fail_sync;
static int destination_flags;

int cp_mock_stat(const char *path, struct stat *info) {
    (void)path;
    info->is_dir = 0;
    info->size = sizeof(source_bytes);
    return 0;
}
int cp_mock_open(const char *path, int flags) {
    if (kstreq(path, "source")) return flags == O_RDONLY ? 3 : -1;
    destination_flags = flags;
    if (reject_existing) return -1;
    destination_size = 0;
    return 4;
}
ssize_t cp_mock_read(int fd, void *output, size_t size) {
    CHECK(fd == 3, "cp reads source descriptor");
    if (size > 733) size = 733;
    if (size > sizeof(source_bytes) - source_position) size = sizeof(source_bytes) - source_position;
    unsigned char *p = output;
    for (size_t i = 0; i < size; i++) p[i] = source_bytes[source_position + i];
    source_position += (unsigned)size;
    return (ssize_t)size;
}
ssize_t cp_mock_write(int fd, const void *input, size_t size) {
    if (fd == 1) return (ssize_t)size;
    CHECK(fd == 4, "cp writes destination descriptor");
    if (fail_write && destination_size >= 200) return -1;
    if (size > 37) size = 37;
    CHECK(destination_size + size <= sizeof(destination_bytes), "destination remains bounded");
    const unsigned char *p = input;
    for (size_t i = 0; i < size; i++) destination_bytes[destination_size + i] = p[i];
    destination_size += (unsigned)size;
    return (ssize_t)size;
}
int cp_mock_close(int fd) {
    CHECK(fd == 3 || fd == 4, "cp closes owned descriptors");
    close_count++;
    return 0;
}
int cp_mock_sync(void) { sync_count++; return fail_sync ? -1 : 0; }

static void reset(void) {
    destination_size = source_position = close_count = sync_count = 0;
    reject_existing = fail_write = fail_sync = false;
    destination_flags = 0;
    for (unsigned i = 0; i < sizeof(source_bytes); i++) source_bytes[i] = (unsigned char)(i * 19 + 7);
}
static int copy_file(void) {
    char *argv[] = {"cp", "source", "destination"};
    return cp_test_main(3, argv);
}
static void partial_io(void) {
    reset();
    CHECK(copy_file() == 0 && destination_size == sizeof(source_bytes), "cp completes partial reads/writes");
    unsigned differences = 0;
    for (unsigned i = 0; i < sizeof(source_bytes); i++) differences += source_bytes[i] != destination_bytes[i];
    CHECK(differences == 0, "copied bytes match source");
    CHECK(destination_flags == (O_WRONLY | O_CREAT | O_EXCL), "cp creates exclusively without truncate");
    CHECK(close_count == 2 && sync_count == 1, "cp closes both handles and syncs before success");
}
static void existing_and_failures(void) {
    reset();
    reject_existing = true;
    destination_size = 123;
    CHECK(copy_file() == 1 && destination_size == 123 && source_position == 0,
          "existing destination rejected without source reads or truncation");
    CHECK(close_count == 1 && sync_count == 1, "failed exclusive creation releases source and syncs");
    reset();
    fail_write = true;
    CHECK(copy_file() == 1 && destination_size > 0 && destination_size < sizeof(source_bytes),
          "failed copy leaves partial destination available");
    CHECK(close_count == 2 && sync_count == 1, "partial failure closes both handles and syncs");
    reset();
    fail_sync = true;
    CHECK(copy_file() == 1 && destination_size == sizeof(source_bytes), "sync failure is not reported as success");
}
TEST_MAIN(partial_io, existing_and_failures)
