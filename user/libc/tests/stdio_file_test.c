// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for file streams (src/stdio/fopen.c, fread.c, fseek.c,
// remove.c) over the fake kernel's in-memory filesystem, which follows the
// kernel's rules (tests/fake_syscall.c): every fopen mode, reading by byte,
// line and block, update streams switching between reading and writing
// without fseek, positioning, ungetc, errno for failed opens, remove,
// rename, exit() closing open files, stdin from the console, and freopen.

#include "test.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_syscall.h"

static int file_is(const char *path, const char *want) {
    size_t len;
    const char *data = fake_file_data(path, &len);
    return data != NULL && len == strlen(want) && memcmp(data, want, len) == 0;
}

static void reset(void) {
    fake_reset_files();
    fake_reset_console();
    errno = 0;
}

static void test_write_and_read_back(void) {
    reset();
    FILE *f = fopen("notes.txt", "w");
    CHECK(f != NULL && fake_open_fds() == 1, "fopen w creates");
    CHECK(fprintf(f, "line %d\n", 1) == 7 && fputs("line two\nno newline", f) == 0, "writes");
    CHECK(file_is("notes.txt", ""), "fully buffered: nothing written before fclose");
    CHECK(fclose(f) == 0 && fake_open_fds() == 0, "fclose closes the fd");
    CHECK(file_is("notes.txt", "line 1\nline two\nno newline"), "contents after fclose");

    f = fopen("notes.txt", "r");
    char buf[32];
    CHECK(fgets(buf, sizeof buf, f) != NULL && strcmp(buf, "line 1\n") == 0, "fgets a line");
    CHECK(fgetc(f) == 'l' && getc(f) == 'i', "fgetc, getc");
    CHECK(fgets(buf, 4, f) != NULL && strcmp(buf, "ne ") == 0, "fgets stops at n - 1");
    CHECK(fgets(buf, sizeof buf, f) != NULL && strcmp(buf, "two\n") == 0, "the rest of the line");
    CHECK(fgets(buf, sizeof buf, f) != NULL && strcmp(buf, "no newline") == 0,
          "a last line without a newline");
    CHECK(!feof(f) || 1, "feof may already be set");
    CHECK(fgets(buf, sizeof buf, f) == NULL && feof(f) && !ferror(f), "then end of file");
    CHECK(fgetc(f) == EOF, "fgetc at end of file");
    CHECK(fgets(buf, 1, f) == NULL || buf[0] == '\0', "fgets with n = 1");
    fclose(f);
}

static void test_modes(void) {
    reset();
    fake_put_file("log", "start\n", 6);
    FILE *f = fopen("log", "a");
    fputs("more\n", f);
    fclose(f);
    CHECK(file_is("log", "start\nmore\n"), "a appends");

    f = fopen("log", "w");
    fclose(f);
    CHECK(file_is("log", ""), "w truncates");

    fake_put_file("log", "abc", 3);
    f = fopen("log", "a+");
    char buf[8] = {0};
    CHECK(fread(buf, 1, 3, f) == 3 && strcmp(buf, "abc") == 0, "a+ reads from the start");
    fseek(f, 0, SEEK_SET);
    fputs("de", f);  // still appended
    fclose(f);
    CHECK(file_is("log", "abcde"), "a+ writes at the end whatever the position");

    f = fopen("log", "rb");
    CHECK(f != NULL && fputc('x', f) == EOF && ferror(f), "r is read only");
    fclose(f);
    f = fopen("log", "wb");
    CHECK(f != NULL && fgetc(f) == EOF && ferror(f), "w is write only");
    fclose(f);

    errno = 0;
    CHECK(fopen("log", "q") == NULL && errno == EINVAL, "an unknown mode: EINVAL");
    errno = 0;
    CHECK(fopen("log", "rw") == NULL && errno == EINVAL, "rw is not a mode");
    errno = 0;
    CHECK(fopen("log", "rx") == NULL && errno == EINVAL, "x only goes with w");
    CHECK(fake_open_fds() == 0, "failed opens leave no fd");
}

static void test_update_streams(void) {
    reset();
    fake_put_file("data", "0123456789", 10);
    FILE *f = fopen("data", "r+");
    CHECK(fgetc(f) == '0' && fgetc(f) == '1', "r+ reads");
    // Writing right after reading (no fseek) writes at the logical position,
    // although the stream read ahead a whole buffer.
    CHECK(fputs("ab", f) == 0, "then writes");
    CHECK(fgetc(f) == '4', "and reads on after what it wrote");
    fclose(f);
    CHECK(file_is("data", "01ab456789"), "r+ overwrote in place");

    f = fopen("fresh", "w+");
    fputs("hello", f);
    CHECK(fgetc(f) == EOF && feof(f), "w+ reading right after writing: at the end");
    rewind(f);
    char buf[8] = {0};
    CHECK(fread(buf, 1, 8, f) == 5 && strcmp(buf, "hello") == 0, "w+ reads back after rewind");
    fclose(f);
}

static void test_positioning(void) {
    reset();
    fake_put_file("pos", "abcdefghij", 10);
    FILE *f = fopen("pos", "r+");
    CHECK(ftell(f) == 0, "ftell at the start");
    fgetc(f);
    fgetc(f);
    CHECK(ftell(f) == 2, "ftell counts what was consumed, not what was read ahead");
    CHECK(fseek(f, 3, SEEK_CUR) == 0 && fgetc(f) == 'f', "SEEK_CUR");
    CHECK(fseek(f, -2, SEEK_END) == 0 && fgetc(f) == 'i' && ftell(f) == 9, "SEEK_END");
    CHECK(fseek(f, 0, SEEK_SET) == 0 && fgetc(f) == 'a', "SEEK_SET");
    fputs("XY", f);
    CHECK(ftell(f) == 3, "ftell counts buffered output");
    errno = 0;
    CHECK(fseek(f, 11, SEEK_SET) == -1 && errno == EINVAL, "no seeking past the end");
    errno = 0;
    CHECK(fseek(f, -1, SEEK_SET) == -1 && errno == EINVAL, "nor before the start");
    CHECK(fseek(f, 0, 7) == -1 && errno == EINVAL, "an unknown whence");
    fseek(f, 0, SEEK_END);
    CHECK(fgetc(f) == EOF && feof(f), "at the end");
    CHECK(fseek(f, 0, SEEK_SET) == 0 && !feof(f), "fseek clears end of file");
    fpos_t mark;
    fseek(f, 4, SEEK_SET);
    CHECK(fgetpos(f, &mark) == 0, "fgetpos");
    fgetc(f);
    CHECK(fsetpos(f, &mark) == 0 && fgetc(f) == 'e', "fsetpos returns there");
    fclose(f);
    CHECK(file_is("pos", "aXYdefghij"), "the write landed after 'a'");
    errno = 0;
    CHECK(fseek(stdin, 0, SEEK_SET) == -1 && errno == ESPIPE && ftell(stdout) == -1,
          "the console cannot seek");
}

static void test_ungetc(void) {
    reset();
    fake_put_file("u", "xy", 2);
    FILE *f = fopen("u", "r");
    CHECK(fgetc(f) == 'x' && ungetc('Q', f) == 'Q', "ungetc");
    CHECK(ftell(f) == 0, "ftell steps back over the pushed byte");
    CHECK(ungetc('R', f) == EOF, "one byte of push-back");
    CHECK(fgetc(f) == 'Q' && fgetc(f) == 'y', "the pushed byte comes first");
    CHECK(fgetc(f) == EOF && feof(f) && ungetc('z', f) == 'z' && !feof(f),
          "ungetc clears end of file");
    char buf[4] = {0};
    CHECK(fread(buf, 1, 3, f) == 1 && buf[0] == 'z', "fread takes the pushed byte");
    CHECK(ungetc(EOF, f) == EOF, "ungetc(EOF) does nothing");
    fclose(f);
}

static void test_blocks(void) {
    reset();
    static unsigned char big[5000], back[5000];
    for (size_t i = 0; i < sizeof big; i++) {
        big[i] = (unsigned char)(i * 7 + (i >> 8));  // includes zero bytes
    }
    FILE *f = fopen("bin", "wb");
    CHECK(fwrite(big, 10, 500, f) == 500, "fwrite 5000 bytes");
    fclose(f);
    size_t len;
    fake_file_data("bin", &len);
    CHECK(len == 5000, "5000 bytes stored (%zu)", len);
    f = fopen("bin", "rb");
    CHECK(fread(back, 1, 3, f) == 3, "a small read first");
    CHECK(fread(back + 3, 1, 4997, f) == 4997, "then a large one past the buffer");
    CHECK(memcmp(big, back, sizeof big) == 0, "binary data intact");
    CHECK(fread(back, 1, 1, f) == 0 && feof(f), "end of file");
    CHECK(fread(back, 0, 5, f) == 0 && fread(back, 5, 0, f) == 0, "fread of nothing");
    fclose(f);
    f = fopen("bin", "r");
    CHECK(fread(back, 3, 2000, f) == 1666, "fread counts whole members (%d)", 1666);
    fclose(f);
}

static void test_open_errors(void) {
    reset();
    errno = 0;
    CHECK(fopen("missing", "r") == NULL && errno == ENOENT, "missing file: ENOENT");
    fake_put_file("exists", "x", 1);
    errno = 0;
    CHECK(fopen("exists", "wx") == NULL && errno == EEXIST, "wx on an existing file: EEXIST");
    FILE *f = fopen("new", "wx");
    CHECK(f != NULL, "wx creates a new file");
    fclose(f);
    fake_mkdir("dir");
    errno = 0;
    CHECK(fopen("dir", "w") == NULL && errno == EISDIR, "a directory for writing: EISDIR");
    fake_set_readonly("exists", 1);
    errno = 0;
    CHECK(fopen("exists", "a") == NULL && errno == EACCES, "a read-only file: EACCES");
    CHECK((f = fopen("exists", "r")) != NULL, "but it reads");
    fclose(f);

    FILE *many[20];
    int opened = 0;
    for (int i = 0; i < 20; i++) {
        many[i] = fopen("exists", "r");
        opened += many[i] != NULL;
    }
    CHECK(opened == 16, "16 files at once, as FOPEN_MAX allows (%d)", opened);
    for (int i = 0; i < 20; i++) {
        if (many[i] != NULL) {
            fclose(many[i]);
        }
    }
    CHECK(fake_open_fds() == 0, "all closed again");
}

static void test_remove_rename(void) {
    reset();
    fake_put_file("a", "1", 1);
    fake_mkdir("d");
    CHECK(rename("a", "b") == 0 && file_is("b", "1") && fake_file_data("a", NULL) == NULL,
          "rename");
    errno = 0;
    CHECK(rename("a", "c") == -1 && errno == ENOENT, "rename a missing file: ENOENT");
    fake_put_file("c", "2", 1);
    errno = 0;
    CHECK(rename("b", "c") == -1 && errno == EEXIST, "rename onto an existing name: EEXIST");
    CHECK(remove("b") == 0 && fake_file_data("b", NULL) == NULL, "remove a file");
    CHECK(remove("d") == 0 && fake_file_data("d", NULL) == NULL, "remove a directory");
    errno = 0;
    CHECK(remove("b") == -1 && errno == ENOENT, "remove a missing file: ENOENT");
    fake_set_readonly("c", 1);
    errno = 0;
    CHECK(remove("c") == -1 && errno == EACCES, "remove a read-only file: EACCES");
}

static fake_jmp_buf env;

static void test_exit_closes_files(void) {
    reset();
    FILE *f = fopen("unclosed", "w");
    fputs("buffered, never closed", f);
    if (setjmp(&env) == 0) {
        fake_arm_exit(&env);
        exit(0);
    }
    CHECK(file_is("unclosed", "buffered, never closed"), "exit flushes open files");
    CHECK(fake_open_fds() == 0, "and closes them");
}

static void test_stdin(void) {
    reset();
    static const char typed[] = "yes\rrest of the input\nlast";
    fake_set_input(typed, sizeof typed - 1, 0);
    printf("Continue? ");
    CHECK(strcmp(fake_output(1, NULL), "") == 0, "the prompt is still buffered");
    CHECK(getchar() == 'y', "getchar");
    CHECK(strcmp(fake_output(1, NULL), "Continue? ") == 0, "reading stdin flushed the prompt");
    CHECK(getchar() == 'e' && getchar() == 's' && getchar() == '\r',
          "raw bytes: Enter arrives as \\r");
    char line[64];
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "rest of the input\n") == 0,
          "fgets on stdin");
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "last") == 0 && feof(stdin),
          "the end of the input");
    clearerr(stdin);
}

static void test_freopen(void) {
    reset();
    fake_put_file("in.txt", "from a file\n", 12);
    char line[32];
    CHECK(freopen("in.txt", "r", stdin) == stdin, "freopen stdin");
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "from a file\n") == 0,
          "stdin now reads the file");
    CHECK(freopen("out.txt", "w", stdout) == stdout, "freopen stdout");
    printf("redirected %d\n", 7);
    fflush(stdout);
    CHECK(file_is("out.txt", "redirected 7\n"), "stdout now writes the file");
    CHECK(strcmp(fake_output(1, NULL), "") == 0, "nothing on the console");
    errno = 0;
    CHECK(freopen(NULL, "r", stdin) == NULL && errno == EINVAL, "freopen(NULL) is unsupported");
    CHECK(fclose(stdout) == 0 && fake_open_fds() == 0, "closing them closes the files");
}

TEST_MAIN(test_write_and_read_back, test_modes, test_update_streams, test_positioning, test_ungetc,
          test_blocks, test_open_errors, test_remove_rename, test_exit_closes_files, test_stdin,
          test_freopen)
