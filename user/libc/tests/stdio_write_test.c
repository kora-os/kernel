// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests for stream output (src/stdio/file.c, fwrite.c, printf.c) over the
// fake kernel's console (tests/fake_syscall.c): when each buffering mode
// writes, short and failing writes, return values and error indicators,
// fflush, setvbuf, perror, the flush at exit() (and none at _Exit), and whole
// lines from several threads printing at once.

#include "test.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_syscall.h"

typedef unsigned long host_thread_t;
int pthread_create(host_thread_t *thread, const void *attr, void *(*start)(void *), void *arg);
int pthread_join(host_thread_t thread, void **result);

static const char *out(void) {
    return fake_output(1, NULL);
}

static const char *err(void) {
    return fake_output(2, NULL);
}

// Back to the start-up state: line buffered stdout, unbuffered stderr.
static void reset(void) {
    fflush(NULL);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    clearerr(stdout);
    clearerr(stderr);
    fake_reset_console();
}

static void test_line_buffered_stdout(void) {
    reset();
    CHECK(printf("abc") == 3, "printf returns the count");
    CHECK(fake_writes(1) == 0 && strcmp(out(), "") == 0, "no newline: nothing written yet");
    CHECK(printf("def\n%d", 42) == 6, "printf with a newline");
    CHECK(fake_writes(1) == 1 && strcmp(out(), "abcdef\n") == 0,
          "the newline flushes the buffer in one write, 42 stays: \"%s\"", out());
    CHECK(puts("line") >= 0 && strcmp(out(), "abcdef\n42line\n") == 0, "puts adds a newline");
    CHECK(putchar('x') == 'x' && fake_writes(1) == 2, "putchar buffers");
    CHECK(fflush(stdout) == 0 && strcmp(out(), "abcdef\n42line\nx") == 0, "fflush writes");
    CHECK(fflush(stdout) == 0 && fake_writes(1) == 3, "fflush of an empty buffer writes nothing");
}

static void test_unbuffered_stderr(void) {
    reset();
    CHECK(fputs("oops", stderr) == 0 && strcmp(err(), "oops") == 0, "stderr writes at once");
    CHECK(fprintf(stderr, " %s=%d\n", "x", 5) == 5 && strcmp(err(), "oops x=5\n") == 0,
          "fprintf to stderr");
    CHECK(fputc('!', stderr) == '!' && strcmp(err(), "oops x=5\n!") == 0, "fputc to stderr");
    CHECK(strcmp(out(), "") == 0, "stdout untouched");
}

static void test_full_buffering(void) {
    reset();
    CHECK(setvbuf(stdout, NULL, _IOFBF, 0) == 0, "setvbuf to fully buffered");
    printf("one\ntwo\n");
    CHECK(fake_writes(1) == 0, "newlines do not flush a fully buffered stream");
    char chunk[100];
    memset(chunk, 'z', sizeof chunk);
    for (int i = 0; i < 12; i++) {
        fwrite(chunk, 1, sizeof chunk, stdout);
    }
    CHECK(fake_writes(1) == 1, "a full buffer (BUFSIZ) is written once (%u)", fake_writes(1));
    fflush(stdout);
    size_t len;
    fake_output(1, &len);
    CHECK(len == 8 + 1200, "everything arrives (%zu bytes)", len);

    reset();
    char user[16];
    CHECK(setvbuf(stdout, user, _IOFBF, sizeof user) == 0, "setvbuf with the caller's buffer");
    fputs("0123456789abcde", stdout);
    CHECK(fake_writes(1) == 0, "15 bytes fit in 16");
    fputs("fg", stdout);
    CHECK(fake_writes(1) == 1 && strcmp(out(), "0123456789abcdef") == 0,
          "the 16th byte fills and flushes: \"%s\"", out());
    fflush(stdout);
    CHECK(strcmp(out(), "0123456789abcdefg") == 0, "the rest follows");
    reset();  // drops the caller's buffer

    // Writes at least as large as the buffer bypass it.
    static char big[5000];
    memset(big, 'b', sizeof big - 1);
    printf("<");
    CHECK(printf("%s", big) == 4999 && fake_writes(1) >= 1, "a large printf");
    fflush(stdout);
    size_t n;
    fake_output(1, &n);
    CHECK(n == 5000 && out()[0] == '<', "large output intact (%zu)", n);
}

static void test_unbuffered_stdout(void) {
    reset();
    setvbuf(stdout, NULL, _IONBF, 0);
    putchar('a');
    CHECK(strcmp(out(), "a") == 0 && fake_writes(1) == 1, "_IONBF writes each call at once");
    setbuf(stdout, NULL);
    putchar('b');
    CHECK(strcmp(out(), "ab") == 0, "setbuf(NULL) is unbuffered");
    CHECK(setvbuf(stdout, NULL, 99, 0) != 0, "setvbuf rejects an unknown mode");
}

static void test_short_writes(void) {
    reset();
    fake_set_write_chunk(3);
    CHECK(printf("a line much longer than three bytes\n") == 36, "printf");
    CHECK(strcmp(out(), "a line much longer than three bytes\n") == 0,
          "short writes are retried until done: \"%s\"", out());
    CHECK(fake_writes(1) == 12, "36 bytes in 3-byte writes (%u)", fake_writes(1));
    fputs("unbuffered too", stderr);
    CHECK(strcmp(err(), "unbuffered too") == 0, "and on stderr");
}

static void test_write_errors(void) {
    reset();
    fake_fail_writes(1);
    errno = 0;
    CHECK(printf("lost\n") == -1 && errno == EIO, "printf reports a failed write");
    CHECK(ferror(stdout) && !feof(stdout), "the error indicator is set");
    CHECK(puts("x") == EOF && fputc('y', stdout) != EOF, "puts fails (its newline flushes)");
    CHECK(fflush(stdout) == EOF, "fflush of the buffered y fails");
    CHECK(fputs("e", stderr) == EOF && fputc('e', stderr) == EOF, "stderr writes fail at once");
    CHECK(fwrite("abc", 1, 3, stderr) == 0, "fwrite returns what was written");
    fake_fail_writes(0);
    clearerr(stdout);
    CHECK(!ferror(stdout), "clearerr");
    CHECK(printf("ok\n") == 3 && strcmp(out(), "ok\n") == 0, "the stream works again");
    CHECK(fflush(NULL) == 0, "fflush(NULL) with nothing failing");
}

static void test_return_values(void) {
    reset();
    CHECK(fputc(0x1ff, stdout) == 0xff, "fputc returns the byte as unsigned char");
    CHECK(putc('q', stdout) == 'q', "putc");
    CHECK(fwrite("abcdef", 2, 3, stdout) == 3, "fwrite counts members");
    CHECK(fwrite("abc", 0, 3, stdout) == 0 && fwrite("abc", 3, 0, stdout) == 0,
          "fwrite of nothing");
    CHECK(fwrite("x", (size_t)-1, 2, stdout) == 0 && errno == EOVERFLOW,
          "fwrite size * nmemb overflow");
    fflush(stdout);
    CHECK(memcmp(out(), "\xffqabcdef", 8) == 0, "all of it arrived");
    CHECK(fputc('x', stdin) == EOF && ferror(stdin), "writing to stdin is an error");
    clearerr(stdin);
}

static void test_perror(void) {
    reset();
    errno = ENOENT;
    perror("open");
    CHECK(strcmp(err(), "open: No such file or directory\n") == 0, "perror: \"%s\"", err());
    fake_reset_console();
    errno = ERANGE;
    perror(NULL);
    perror("");
    CHECK(strcmp(err(), "Result out of range\nResult out of range\n") == 0,
          "perror without a prefix: \"%s\"", err());
}

static fake_jmp_buf env;

static void late_output(void) {
    printf("from the handler");
}

static void test_exit_flushes(void) {
    reset();
    printf("pending");
    atexit(late_output);
    if (setjmp(&env) == 0) {
        fake_arm_exit(&env);
        exit(3);
    }
    CHECK(fake_exit_status() == 3, "exit status");
    CHECK(strcmp(out(), "pendingfrom the handler") == 0,
          "exit flushes stdout after the atexit handlers: \"%s\"", out());

    reset();
    printf("dropped");
    if (setjmp(&env) == 0) {
        fake_arm_exit(&env);
        _Exit(0);
    }
    CHECK(strcmp(out(), "") == 0, "_Exit does not flush");
    fake_reset_console();  // forget the dropped bytes before the next test
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
}

#define THREADS 4
#define LINES 300

static void *print_lines(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < LINES; i++) {
        printf("thread %ld line %03d: %s\n", id, i, "some text to make the line longer");
    }
    return NULL;
}

static void test_threads(void) {
    reset();
    setvbuf(stdout, NULL, _IOFBF, 0);
    host_thread_t t[THREADS];
    for (long i = 0; i < THREADS; i++) {
        pthread_create(&t[i], NULL, print_lines, (void *)i);
    }
    for (int i = 0; i < THREADS; i++) {
        pthread_join(t[i], NULL);
    }
    fflush(stdout);
    // Every line must be whole: count the well-formed ones per thread.
    int seen[THREADS] = {0};
    int bad = 0;
    const char *p = out();
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (nl == NULL) {
            bad++;
            break;
        }
        long id = p[7] - '0';
        // "thread N line MMM: " is 19 characters, the text 33 more.
        if (strncmp(p, "thread ", 7) != 0 || id < 0 || id >= THREADS || nl - p != 52 ||
            strncmp(p + 19, "some text to make the line longer", 33) != 0) {
            bad++;
        } else {
            seen[id]++;
        }
        p = nl + 1;
    }
    CHECK(bad == 0, "%d torn lines", bad);
    for (int i = 0; i < THREADS; i++) {
        CHECK(seen[i] == LINES, "thread %d: %d whole lines", i, seen[i]);
    }
    reset();
}

TEST_MAIN(test_line_buffered_stdout, test_unbuffered_stderr, test_full_buffering,
          test_unbuffered_stdout, test_short_writes, test_write_errors, test_return_values,
          test_perror, test_exit_flushes, test_threads)
