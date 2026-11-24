// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of a console on a screen (user/libscreen/console.c) on the fake
// kernel: its programs' packets (writes shown, reads held until a line is
// ready and answered in order, modes), readers that end, Ctrl+C, the
// scrollback keys, and the rows it reports changed to the screen manager. The
// screen and the manager are played here.

#include "test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fake_syscall.h"
#include "libscreen/console.h"

#define WIDTH 320   // 40 columns
#define HEIGHT 96   // 12 rows
#define MANAGER_PORT 3
#define CONSOLE_PORT 7
#define PORT_BIT 30
#define END_BIT 29
#define KEY_BIT 20
#define KEY_PAGE_UP 0x4B

static int shared;  // the manager's name for the screen
// The keys typed: a message of the manager's on the screen's port.
#define EVENT_PORT 8
#define MANAGER_PID 99
static struct screen_events typed;
static int typed_out, typed_replies, gone_test;
static unsigned pixels[WIDTH * HEIGHT];
static screen_t handle;

// The console's port: messages queued for it, as (packet, sender).
static struct {
    struct con_packet *packet;
    int sender;
} queue[32];
static int queued, taken;
// What it answered, in order.
static struct con_packet *answered[64];
static int answer_count;
// Damage reported to the manager.
static int damage_y, damage_height, damages;
static struct screen_request *manager_request;
static int ended[100], watched[100];
static int sig_next = PORT_BIT;
// What the console copied to the serial line (conlog).
static char logged[4096];
static size_t logged_length;

// console_run's program, and what was done to it.
#define RUN_PID 77
static int run_flags, run_std_ok, waits, sent_to, killed;
static unsigned sent_mask;
static void type_key(unsigned keycode, unsigned modifiers, unsigned codepoint);

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a2;
    (void)a3;
    switch (number) {
    case SYS_PORT_CREATE:
        return CONSOLE_PORT;
    case SYS_PORT_DELETE:
    case SYS_PORT_SIGNAL:
    case SYS_SIG_FREE:
        return 0;
    case SYS_SIG_ALLOC:
        return sig_next--;
    case SYS_PORT_GET: {
        if (a0 == EVENT_PORT) {
            if (typed.count == 0 || typed_out) {
                return 0;
            }
            typed_out = 1;
            *(struct port_message *)a1 = (struct port_message){&typed, PORT_MESSAGE, MANAGER_PID};
            return 1;
        }
        if (a0 != CONSOLE_PORT || taken == queued) {
            return 0;
        }
        struct port_message *out = (struct port_message *)a1;
        *out = (struct port_message){queue[taken].packet, PORT_MESSAGE, queue[taken].sender};
        taken++;
        return 1;
    }
    case SYS_PORT_REPLY:
        if ((void *)a0 == &typed) {
            CHECK(typed_out, "the keys' message is replied once");
            typed_out = 0;
            typed.count = 0;
            typed_replies++;
            return 0;
        }
        answered[answer_count++] = (struct con_packet *)a0;
        return 0;
    case SYS_PORT_PUT: {  // screen_damage to the manager
        CHECK(a0 == MANAGER_PORT, "requests go to the manager");
        manager_request = (struct screen_request *)a1;
        CHECK(manager_request->op == SCREEN_DAMAGE && manager_request->screen == &shared,
              "the console reports damage on its screen");
        damage_y = manager_request->y;
        damage_height = manager_request->height;
        CHECK(manager_request->x == 0 && manager_request->width == WIDTH, "whole rows");
        manager_request->result = 0;
        damages++;
        return 0;
    }
    case SYS_PORT_WAIT: {
        struct port_message *out = (struct port_message *)a1;
        *out = (struct port_message){manager_request, PORT_REPLY, 1};
        return 1;
    }
    case SYS_CONLOG:
        memcpy(logged + logged_length, (const char *)a0, (size_t)a1);
        logged_length += (size_t)a1;
        logged[logged_length] = '\0';
        return a1;
    case SYS_SPAWN_STD: {  // console_run's program
        const struct spawn_request *r = (const struct spawn_request *)a0;
        run_flags = r->flags;
        run_std_ok = 1;
        for (int i = 0; i < 3; i++) {
            run_std_ok &= r->std[i].kind == STD_PORT && r->std[i].value == CONSOLE_PORT;
        }
        return strcmp(r->name, "nosuch") == 0 ? -1 : RUN_PID;
    }
    case SYS_SIG_WAIT: {  // while console_run serves: Ctrl+C, a stray end, its end
        int end = ((run_flags >> 12) & 0x3f) - 1;
        CHECK((a0 & SIG_MASK(end)) && (a0 & SIG_MASK(KEY_BIT)), "it waits for keys and the end");
        waits++;
        if (waits == 1 && gone_test) {
            return SIG_MASK(KEY_BIT);  // the manager's end
        }
        if (waits == 1) {
            type_key(0x06, INPUT_MOD_LEFT_CTRL, 'c');
            return SIG_MASK(KEY_BIT);
        }
        return SIG_MASK(end);
    }
    case SYS_WAITPID:
        CHECK(a0 == RUN_PID && a2 == WNOHANG, "it reaps its program without blocking");
        if (waits < 3) {
            return 0;
        }
        *(int *)a1 = 5;
        return RUN_PID;
    case SYS_SIG_SEND:
        sent_to = (int)a0;
        sent_mask = (unsigned)a1;
        return 0;
    case SYS_KILL:
        killed = (int)a0;
        return 0;
    case SYS_SIG_ON_END:
        if (a0 <= 0 || a0 >= 100 || ended[a0]) {
            return -1;
        }
        watched[a0] = a1 != -1;
        return 0;
    default:
        return -1;
    }
}

static void put(struct con_packet *packet, int sender) {
    queue[queued].packet = packet;
    queue[queued].sender = sender;
    queued++;
}

static void type_key(unsigned keycode, unsigned modifiers, unsigned codepoint) {
    CHECK(!typed_out && typed.count < SCREEN_BATCH, "keys typed between services");
    typed.events[typed.count++] = (struct input_event){
        .type = INPUT_EVENT_KEY_DOWN, .modifiers = (unsigned short)modifiers,
        .keycode = (unsigned short)keycode, .codepoint = codepoint};
}

static void type_text(const char *text) {
    for (; *text; text++) {
        if (*text == '\n') {
            type_key(KEY_ENTER, 0, 0);
        } else {
            type_key(KEY_A, 0, (unsigned char)*text);  // the character is what counts
        }
    }
}

static console_t *c;

static void service(unsigned got) {
    console_service(c, got);
}

static int broke;
static void on_break(void *ctx) {
    CHECK(ctx == &broke, "the break callback's context");
    broke++;
}

static const char *row_text(uint32_t row) {
    static char text[64];
    uint32_t n = 0;
    for (uint32_t col = 0; col < c->term.cols; col++) {
        uint8_t ch = term_view_cell(&c->term, row, col)->ch;
        text[n++] = ch ? (char)ch : ' ';
    }
    while (n > 0 && text[n - 1] == ' ') {
        n--;
    }
    text[n] = '\0';
    return text;
}

static void setup(void) {
    fake_set_handler(kernel);
    handle = (screen_t){.width = WIDTH, .height = HEIGHT, .pitch = WIDTH * 4, .pixels = pixels,
                        .id = 1, .screen = &shared, .manager = MANAGER_PORT,
                        .manager_pid = MANAGER_PID, .reply_port = 4, .event_port = EVENT_PORT,
                        .bit = KEY_BIT};
    c = console_open(&handle);
    CHECK(c != NULL && c->term.cols == 40 && c->term.rows == 12, "a console of 40x12 cells");
    CHECK(damages == 1 && damage_y == 0 && damage_height == HEIGHT, "first drawn whole");
    CHECK(console_port(c) == CONSOLE_PORT, "its port");
    CHECK(console_signals(c) == (SIG_MASK(KEY_BIT) | SIG_MASK(PORT_BIT) | SIG_MASK(END_BIT)),
          "its signals: keys, port, readers' ends");
    console_on_break(c, on_break, &broke);
}

static void test_writes(void) {
    static char text[] = "hello\nworld";
    struct con_packet write = {CON_WRITE, -9, text, strlen(text)};
    put(&write, 50);
    damages = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && answered[0] == &write && write.result == (long)strlen(text),
          "a write is answered with its length");
    CHECK(strcmp(row_text(0), "hello") == 0 && strcmp(row_text(1), "world") == 0,
          "and shown, its newline starting a line");
    CHECK(damages == 1 && damage_y == 0 && damage_height == 2 * TERM_CELL_HEIGHT,
          "only the rows that changed are copied (%d, %d)", damage_y, damage_height);
    damages = 0;
    service(0);
    CHECK(damages == 0, "nothing changed, nothing copied");
    console_write(c, "\n$ ", 3);
    CHECK(strcmp(row_text(2), "$") == 0 && c->term.col == 2, "the client's own text");
}

static void test_reads(void) {
    static char a[16], b[16];  // reads still waiting are answered by the tests after
    struct con_packet first = {CON_READ, -9, a, sizeof(a)};
    struct con_packet second = {CON_READ, -9, b, sizeof(b)};
    put(&first, 50);
    put(&second, 51);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 0 && c->waiting_count == 2, "reads wait for a line");
    CHECK(watched[50] && watched[51], "their programs are watched");
    type_text("ab");
    service(SIG_MASK(KEY_BIT));
    CHECK(answer_count == 0 && strcmp(row_text(2), "$ ab") == 0, "typing is echoed, not read yet");
    type_text("\nsecond\n");
    service(SIG_MASK(KEY_BIT));
    CHECK(strstr(logged, "hello\nworld") != NULL && strstr(logged, "ab\nsecond\n") != NULL &&
              strstr(logged, "\x1b") == NULL,
          "conlog gets the writes and the lines entered, not the echo's cursor moves:\n%s", logged);
    CHECK(answer_count == 2 && answered[0] == &first && first.result == 3 &&
          memcmp(a, "ab\n", 3) == 0, "the first read gets the first line");
    CHECK(answered[1] == &second && second.result == 7 && memcmp(b, "second\n", 7) == 0,
          "the second the next one");
    CHECK(!watched[50] && !watched[51], "and their programs are no longer watched");

    struct con_packet empty = {CON_READ, -9, a, 0};
    put(&empty, 50);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && empty.result == 0, "a read of nothing is answered at once");

    // A reader that ends while it waits.
    struct con_packet orphan = {CON_READ, -9, a, sizeof(a)};
    put(&orphan, 60);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    ended[60] = 1;
    service(0);
    CHECK(answer_count == 0, "without its end signal nothing is checked");
    service(SIG_MASK(END_BIT));
    CHECK(answer_count == 1 && orphan.result == -1 && c->waiting_count == 0,
          "an ended reader's packet is answered, so it can be freed");
    struct con_packet late = {CON_READ, -9, a, sizeof(a)};
    put(&late, 60);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && late.result == -1, "a read from a program already gone");

    // End of input.
    struct con_packet eof = {CON_READ, -9, a, sizeof(a)};
    put(&eof, 50);
    type_key(0x07, INPUT_MOD_LEFT_CTRL, 'd');
    answer_count = 0;
    service(SIG_MASK(PORT_BIT) | SIG_MASK(KEY_BIT));
    CHECK(answer_count == 1 && eof.result == 0, "Ctrl+D: a read returns 0");

    // Too many readers.
    static struct con_packet many[CONSOLE_READERS + 1];
    for (int i = 0; i <= CONSOLE_READERS; i++) {
        many[i] = (struct con_packet){CON_READ, -9, a, sizeof(a)};
        put(&many[i], 50);
    }
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && answered[0] == &many[CONSOLE_READERS] &&
          many[CONSOLE_READERS].result == -1, "a read past %d waiting is refused", CONSOLE_READERS);
}

static void test_modes_and_keys(void) {
    struct con_packet raw = {CON_MODE, -9, NULL, CON_RAW};
    put(&raw, 50);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && raw.result == 0 && c->edit.mode == CON_RAW, "raw mode");
    type_key(0x14, 0, 'q');
    type_key(KEY_ENTER, 0, 0);
    answer_count = 0;
    service(SIG_MASK(KEY_BIT));
    CHECK(c->waiting_count == CONSOLE_READERS - 1 && answer_count == 1 &&
          memcmp(answered[0]->buf, "q\n", 2) == 0 && answered[0]->result == 2,
          "in raw mode a waiting read gets the keys at once");
    struct con_packet bad = {42, -9, NULL, 0};
    put(&bad, 50);
    answer_count = 0;
    service(SIG_MASK(PORT_BIT));
    CHECK(answer_count == 1 && bad.result == -1, "an unknown packet is refused");

    type_key(0x06, INPUT_MOD_LEFT_CTRL, 'c');
    service(SIG_MASK(KEY_BIT));
    CHECK(broke == 1, "Ctrl+C calls the break");

    for (int i = 0; i < 30; i++) {
        console_write(c, "more output\n", 12);
    }
    type_key(KEY_PAGE_UP, INPUT_MOD_LEFT_SHIFT, 0);
    service(SIG_MASK(KEY_BIT));
    CHECK(c->term.view_offset == 6, "Shift+PageUp scrolls back half a screen (%u)",
          (unsigned)c->term.view_offset);
    type_key(0x14, 0, 'q');
    service(SIG_MASK(KEY_BIT));
    CHECK(c->term.view_offset == 0, "typing returns to the live screen");
    typed.events[typed.count++] = (struct input_event){.type = INPUT_EVENT_MOUSE, .wheel = 2};
    service(SIG_MASK(KEY_BIT));
    CHECK(c->term.view_offset == 2 * CONSOLE_WHEEL_LINES, "the wheel away from you scrolls back (%u)",
          (unsigned)c->term.view_offset);
    typed.events[typed.count++] = (struct input_event){
        .type = INPUT_EVENT_MOUSE, .x = 5, .y = 7, .buttons = INPUT_BUTTON_LEFT, .wheel = -1};
    service(SIG_MASK(KEY_BIT));
    CHECK(c->term.view_offset == CONSOLE_WHEEL_LINES && c->edit.length == 0,
          "towards you forward; a click types nothing (%u)", (unsigned)c->term.view_offset);
    CHECK(typed.count == 0 && !typed_out, "every key was taken");
}

static void test_run(void) {
    int code = 0;
    CHECK(console_run(c, "nosuch", 1, (char *[]){"nosuch", NULL}, CONSOLE_BREAK_KILL, &code) == -1,
          "a program that cannot start");
    char *argv[] = {"shell", NULL};
    waits = 0;
    CHECK(console_run(c, "shell", 1, argv, CONSOLE_BREAK_SIGNAL, &code) == 0 && code == 5,
          "console_run serves until its program ends, and gives its exit code");
    CHECK(run_std_ok && (run_flags & SPAWN_NOWAIT) && (run_flags >> 12) != 0,
          "the program has the console for its handles, and its end is a signal");
    CHECK(sent_to == RUN_PID && sent_mask == SIG_MASK(SIG_BREAK) && killed == 0,
          "Ctrl+C sends it the break signal");
    CHECK(c->on_break == NULL && broke == 1, "the break callback is the console's own again");

    waits = 0;
    sent_to = 0;
    CHECK(console_run(c, "shell", 1, argv, CONSOLE_BREAK_KILL, &code) == 0 && code == 5,
          "again, killing on Ctrl+C");
    CHECK(killed == RUN_PID && sent_to == 0, "Ctrl+C kills it");
    CHECK(typed.count == 0 && !typed_out, "every key was taken");
}

static void test_manager_ends(void) {
    CHECK(!console_gone(c) && typed_replies > 0, "the manager runs; the keys' messages were replied");
    ended[MANAGER_PID] = 1;
    gone_test = 1;
    waits = 0;
    killed = 0;
    int code = 0;
    char *argv[] = {"shell", NULL};
    CHECK(console_run(c, "shell", 1, argv, CONSOLE_BREAK_SIGNAL, &code) == 0 && code == 5,
          "the manager ends while a program runs");
    CHECK(console_gone(c) && killed == RUN_PID,
          "the screen is gone, and the program is killed: nothing reaches it");
    damages = 0;
    console_write(c, "unseen\n", 7);
    CHECK(damages == 0, "nothing is sent to a manager that has ended");
}

static void test_close(void) {
    int left = c->waiting_count;
    answer_count = 0;
    console_close(c);
    CHECK(left > 0 && answer_count == left, "closing answers the reads still waiting (%d)", left);
    for (int i = 0; i < answer_count; i++) {
        CHECK(answered[i]->result == -1, "with an error");
    }
    fake_set_handler(NULL);
}

TEST_MAIN(setup, test_writes, test_reads, test_modes_and_keys, test_run, test_manager_ends,
          test_close)
