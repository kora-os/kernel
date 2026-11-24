// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host tests of the standard streams on the program's standard handles
// (src/stdio/handle.c): the console, a file, or a console handler's port, as
// the fake kernel's handler hands them out. The handler also plays the console
// handler: a packet put on its port is served at once and its reply waits on
// the reply port. Each case makes the libc ask again (__stdio_std_ready).

#include "test.h"

#include <_libc/console.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"
#include "stdio_impl.h"
#include "syscall.h"

int isatty(int fd);

#define HANDLER_PORT 5
#define PORT_REPLY 2
#define PORT_RETURNED 3

static struct std_handle handles[3];
static int have_handles;  // 0: the kernel knows no standard handles
static unsigned asked;

// The console handler.
static char shown[512];
static size_t shown_len;
static const char *typed;  // what a CON_READ hands out, a byte at a time per request
static unsigned packets, writes, reads, modes, reply_ports;
static long last_mode = -1, mode_result;
// CON_FONT packets: how many, the last one's buffer and length, and the
// text shown before it came.
static unsigned fonts;
static const void *font_buf;
static unsigned long font_length;
static char shown_before_font[64];
static int handler_gone, return_unhandled;
static struct {
    void *msg;
    int reply;
} pending;

struct port_received {
    void *msg;
    int kind;
    int sender;
};

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a3;
    switch (number) {
    case SYS_STD_HANDLES:
        asked++;
        if (!have_handles) {
            return -1;
        }
        memcpy((void *)a0, handles, sizeof handles);
        return 0;
    case SYS_PORT_CREATE:
        CHECK(a0 == 0, "reply ports are anonymous");
        return 100 + (long)reply_ports++;
    case SYS_PORT_DELETE:
        return 0;
    case SYS_PORT_PUT: {
        if (handler_gone || a0 != HANDLER_PORT) {
            return -1;
        }
        CHECK(pending.msg == NULL, "one packet at a time per reply port");
        struct con_packet *p = (struct con_packet *)a1;
        packets++;
        if (!return_unhandled) {
            if (p->op == CON_WRITE) {
                writes++;
                memcpy(shown + shown_len, p->buf, p->length);
                shown_len += p->length;
                shown[shown_len] = '\0';
                p->result = (long)p->length;
            } else if (p->op == CON_READ) {
                reads++;
                size_t n = 0;
                while (n < p->length && typed[n]) {
                    n++;
                }
                memcpy(p->buf, typed, n);
                typed += n;
                p->result = (long)n;
            } else if (p->op == CON_MODE) {
                modes++;
                CHECK(p->buf == NULL, "a mode packet carries no buffer");
                last_mode = (long)p->length;
                p->result = mode_result;
            } else if (p->op == CON_FONT) {
                fonts++;
                font_buf = p->buf;
                font_length = p->length;
                memcpy(shown_before_font, shown, shown_len + 1);
                p->result = mode_result;
            }
        }
        pending.msg = p;
        pending.reply = (int)a2;
        return 0;
    }
    case SYS_PORT_WAIT: {
        struct port_received *out = (struct port_received *)a1;
        CHECK(pending.msg != NULL && pending.reply == a0, "the packet comes back to its reply port");
        out->msg = pending.msg;
        out->kind = return_unhandled ? PORT_RETURNED : PORT_REPLY;
        out->sender = 9;
        pending.msg = NULL;
        return 1;
    }
    default:
        return -1;
    }
}

// The start-up state, before the libc has asked.
static void reset(void) {
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
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    have_handles = 0;
    asked = 0;
    shown_len = 0;
    shown[0] = '\0';
    packets = writes = reads = modes = reply_ports = fonts = 0;
    font_buf = NULL;
    font_length = 0;
    last_mode = -1;
    mode_result = 0;
    handler_gone = return_unhandled = 0;
    pending.msg = NULL;
}

static void set(int i, int kind, int value) {
    handles[i] = (struct std_handle){kind, value};
}

static int open_file(const char *path, int flags) {
    return (int)__syscall2(SYS_OPEN, (long)path, flags);
}

static void test_console(void) {
    reset();
    CHECK(printf("hi\n") == 3 && strcmp(fake_output(1, NULL), "hi\n") == 0,
          "with no standard handles from the kernel, stdout is the console");
    CHECK(asked == 1, "the kernel is asked on the first I/O");
    printf("again\n");
    fputs("err\n", stderr);
    CHECK(asked == 1, "and only once");
    CHECK(isatty(0) && isatty(1) && isatty(2), "the console is a console");
    errno = 0;
    CHECK(!isatty(3) && errno == EBADF && !isatty(-1), "other numbers are not");

    reset();
    have_handles = 1;
    set(0, STD_CONSOLE, 0);
    set(1, STD_CONSOLE, 0);
    set(2, STD_CONSOLE, 0);
    CHECK(isatty(1) && asked == 1, "isatty asks the kernel too");
    CHECK(printf("x\n") == 2 && strcmp(fake_output(1, NULL), "x\n") == 0, "console handles");
}

static void test_files(void) {
    reset();
    have_handles = 1;
    int out = open_file("out.txt", 0x101 | 0x200);  // O_WRONLY | O_CREAT | O_TRUNC
    CHECK(out >= 3, "open an output file");
    set(0, STD_CONSOLE, 0);
    set(1, STD_FILE, out);
    set(2, STD_CONSOLE, 0);
    printf("one\n");
    printf("two\n");
    size_t len = 99;
    CHECK(fake_file_data("out.txt", &len) != NULL && len == 0 && fake_writes(1) == 0,
          "stdout to a file is fully buffered: nothing written after two lines");
    fputs("on the console\n", stderr);
    CHECK(strcmp(fake_output(2, NULL), "on the console\n") == 0, "stderr stays the console");
    errno = 0;
    CHECK(!isatty(1) && errno == ENOTTY && isatty(0) && isatty(2), "a file is not a console");
    CHECK(fflush(stdout) == 0, "flush");
    const char *data = fake_file_data("out.txt", &len);
    CHECK(len == 8 && memcmp(data, "one\ntwo\n", 8) == 0, "the lines land in the file");

    reset();
    have_handles = 1;
    fake_put_file("in.txt", "hello\nworld\n", 12);
    int in = open_file("in.txt", 0);
    set(0, STD_FILE, in);
    set(1, STD_CONSOLE, 0);
    set(2, STD_CONSOLE, 0);
    char line[32];
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "hello\n") == 0 &&
              fgets(line, sizeof line, stdin) != NULL && strcmp(line, "world\n") == 0,
          "stdin reads from a file");
    CHECK(fgets(line, sizeof line, stdin) == NULL && feof(stdin), "to its end");
    CHECK(__stdio_standard(0)->mode == _IOFBF, "a file stdin is read ahead a buffer at a time");

    // The caller's own buffering wins over the default for a file.
    reset();
    have_handles = 1;
    out = open_file("out.txt", 0x101 | 0x200);
    set(0, STD_CONSOLE, 0);
    set(1, STD_FILE, out);
    set(2, STD_CONSOLE, 0);
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("line\n");
    data = fake_file_data("out.txt", &len);
    CHECK(len == 5, "setvbuf before the first output overrides the default (%zu)", len);
}

static void test_console_handler(void) {
    reset();
    have_handles = 1;
    set(0, STD_PORT, HANDLER_PORT);
    set(1, STD_PORT, HANDLER_PORT);
    set(2, STD_PORT, HANDLER_PORT);
    CHECK(printf("abc") == 3 && packets == 0, "a handler's stdout is line buffered");
    CHECK(printf("def\n") == 4 && packets == 1 && writes == 1 && strcmp(shown, "abcdef\n") == 0,
          "a line goes to the handler in one write packet");
    CHECK(fake_writes(1) == 0, "nothing reaches the kernel console");
    fputs("E", stderr);
    CHECK(packets == 2 && strcmp(shown, "abcdef\nE") == 0, "stderr goes to the handler at once");
    CHECK(reply_ports == 2, "one reply port per stream, made on first use (%u)", reply_ports);
    printf("more\n");
    CHECK(reply_ports == 2, "and kept");
    CHECK(isatty(0) && isatty(1) && isatty(2), "a handler is a console");

    typed = "yes\nno";
    char line[16];
    CHECK(fgets(line, sizeof line, stdin) != NULL && strcmp(line, "yes\n") == 0,
          "stdin reads from the handler");
    CHECK(reads == 4, "a byte per read request, as from the console (%u)", reads);
    CHECK(reply_ports == 3, "stdin has a reply port of its own");

    // A handler that is gone, or returns a packet unhandled: an I/O error.
    handler_gone = 1;
    errno = 0;
    CHECK(printf("lost\n") == -1 && ferror(stdout) && errno == EIO,
          "a write to a vanished handler fails with EIO");
    handler_gone = 0;
    clearerr(stdout);
    return_unhandled = 1;
    errno = 0;
    CHECK(fputs("x", stderr) == EOF && ferror(stderr) && errno == EIO,
          "a returned packet fails with EIO");
    return_unhandled = 0;
}

// freopen replaces a standard stream's handle, even before its first use.
static void test_freopen(void) {
    reset();
    have_handles = 1;
    set(0, STD_CONSOLE, 0);
    set(1, STD_PORT, HANDLER_PORT);
    set(2, STD_CONSOLE, 0);
    CHECK(freopen("log.txt", "w", stdout) == stdout, "freopen stdout before any output");
    printf("to the file\n");
    fflush(stdout);
    size_t len = 0;
    const char *data = fake_file_data("log.txt", &len);
    CHECK(packets == 0 && len == 12 && memcmp(data, "to the file\n", 12) == 0,
          "the file, not the handler, gets the output");

    reset();
    have_handles = 1;
    set(1, STD_PORT, HANDLER_PORT);
    printf("x\n");
    CHECK(packets == 1 && reply_ports == 1, "a handler's stdout");
    CHECK(freopen("log.txt", "w", stdout) == stdout, "freopen it");
    printf("y\n");
    fflush(stdout);
    CHECK(packets == 1, "the handler gets nothing more");
    fclose(stdout);
}

static void test_modes(void) {
    reset();
    CHECK(__con_mode(CON_RAW) == -1 && packets == 0, "the kernel console has no modes");
    reset();
    have_handles = 1;
    set(0, STD_FILE, open_file("modes.txt", 0x102));  // O_RDWR | O_CREAT
    set(1, STD_CONSOLE, 0);
    set(2, STD_CONSOLE, 0);
    CHECK(__con_mode(CON_RAW) == -1 && packets == 0, "nor has a file");
    reset();
    have_handles = 1;
    set(0, STD_PORT, HANDLER_PORT);
    set(1, STD_CONSOLE, 0);
    set(2, STD_CONSOLE, 0);
    CHECK(__con_mode(CON_RAW) == 0 && modes == 1 && last_mode == CON_RAW,
          "a handler's stdin gets a CON_MODE packet");
    CHECK(__con_mode(CON_COOKED) == 0 && modes == 2 && last_mode == CON_COOKED, "and back");
    mode_result = -1;
    CHECK(__con_mode(CON_RAW) == -1, "a handler without modes refuses");
    mode_result = 0;
    handler_gone = 1;
    CHECK(__con_mode(CON_RAW) == -1, "a handler that is gone");
}

static void test_fonts(void) {
    static const char psf[] = "a font";
    reset();
    CHECK(__con_font(psf, 6) == -1 && packets == 0, "the kernel console has no fonts");
    reset();
    have_handles = 1;
    set(0, STD_PORT, HANDLER_PORT);
    set(1, STD_FILE, open_file("fonts.txt", 0x102));  // O_RDWR | O_CREAT
    set(2, STD_CONSOLE, 0);
    CHECK(__con_font(psf, 6) == -1 && packets == 0, "a file stdout has none, a handler's stdin aside");
    reset();
    have_handles = 1;
    set(0, STD_CONSOLE, 0);
    set(1, STD_PORT, HANDLER_PORT);
    set(2, STD_CONSOLE, 0);
    printf("before");
    CHECK(__con_font(psf, 6) == 0 && fonts == 1 && font_buf == psf && font_length == 6,
          "a handler's stdout gets a CON_FONT packet with the font, in place");
    CHECK(strcmp(shown_before_font, "before") == 0, "what was written before is shown first");
    CHECK(__con_font(NULL, 0) == 0 && fonts == 2 && font_buf == NULL && font_length == 0,
          "NULL: the built-in font");
    mode_result = -1;
    CHECK(__con_font(psf, 6) == -1, "a handler that refuses it");
    mode_result = 0;
    handler_gone = 1;
    CHECK(__con_font(psf, 6) == -1, "a handler that is gone");
}

TEST_MAIN(test_console, test_files, test_console_handler, test_modes, test_fonts,
          test_freopen)  // last: it closes stdout
