// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of the screen manager (user/screens.c) on the fake kernel: its
// requests, what it copies to the display (host memory with padding that must
// stay untouched) from the programs' own pixels, Meta+M, where the keys go
// (messages on the screen's port, one out at a time, the rest waiting, a
// full queue), messages back after their screen closed or their program
// ended, screens closed by their programs or by their ends, lending, the
// pointer (drawn over the front screen, hidden while typing, moves merged,
// mouse events in the screen's pixels), and how it starts its programs.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main screens_main
#include "../screens.c"
#undef main

#define WIDTH 64
#define HEIGHT 48
#define STRIDE 70  // wider than the picture: the padding must stay untouched
#define UNTOUCHED 0x55555555u
#define RED 0x00FF0000u
#define BLUE 0x000000FFu
static unsigned display_pixels[HEIGHT * STRIDE];

// The fake kernel's answers to what screens asks.
#define MAX_PIDS 200
static int watched[MAX_PIDS], ended[MAX_PIDS];
static int focus = -1;
// The programs' side: each pid's pixels, and the messages of events sent to
// its port (100 + pid), the last one not taken yet.
#define EVENT_PORT(pid) (100 + (pid))
#define MAX_PIXEL_SETS 24
static unsigned pixel_sets[MAX_PIXEL_SETS][WIDTH * HEIGHT];
static int pixel_sets_used;
static struct screen_events *sent[MAX_PIDS];
static unsigned messages_sent;
static int dead_port;  // a port whose program has ended: messages to it fail
static char spawned[8][64];
static int spawn_count, spawn_flags_seen;
static int reaped_pid = -1;
static int fb_held, input_held_bit = -1, refuse_display;

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a3;
    switch (number) {
    case SYS_SIG_ON_END:
        if (a0 <= 0 || a0 >= MAX_PIDS || ended[a0]) {
            return -1;
        }
        watched[a0] = a1 != -1;
        CHECK(a1 == -1 || a1 == end_bit, "programs are watched on the end signal");
        return 0;
    case SYS_FB_CLAIM:
        if (a0 && refuse_display) {
            return -1;
        }
        fb_held = (int)a0;
        return 0;
    case SYS_INPUT_CLAIM:
        CHECK(a0 == -1 || a0 == input_bit, "the keyboard is claimed on the input signal");
        input_held_bit = (int)a0;
        return 0;
    case SYS_GETPID:
        return 2;
    case SYS_INPUT_FOCUS:
        focus = (int)a0;
        return 0;
    case SYS_PORT_PUT:
        CHECK(a0 >= 100 && a0 < 100 + MAX_PIDS && a2 == reply_port,
              "events go to a screen's port, coming back to the manager");
        if (a0 == dead_port) {
            return -1;
        }
        CHECK(sent[a0 - 100] == NULL, "one message out at a time");
        sent[a0 - 100] = (struct screen_events *)a1;
        messages_sent++;
        return 0;
    case SYS_SPAWN_FLAGS: {
        char *const *argv = (char *const *)a2;
        char *out = spawned[spawn_count];
        out[0] = '\0';
        for (long i = 0; i < a1; i++) {
            strcat(out, i ? " " : "");
            strcat(out, argv[i]);
        }
        CHECK(strcmp((const char *)a0, argv[0]) == 0, "a program's name is its argv[0]");
        spawn_flags_seen = (int)a3;
        return strcmp((const char *)a0, "nosuch") == 0 ? -1 : 100 + spawn_count++;
    }
    case SYS_WAITPID:
        CHECK(a2 == WNOHANG, "programs are reaped without waiting");
        if (a0 == reaped_pid) {
            *(int *)a1 = 0;
            return a0;
        }
        return 0;
    default:
        return -1;
    }
}

static void reset_display(void) {
    for (unsigned i = 0; i < HEIGHT * STRIDE; i++) {
        display_pixels[i] = UNTOUCHED;
    }
    display = (struct fb_info){(unsigned long)display_pixels, WIDTH, HEIGHT, STRIDE * 4, 32};
    end_bit = 20;
    input_bit = 21;
    reply_port = 7;
}

static unsigned shown(int x, int y) {
    return display_pixels[y * STRIDE + x];
}

// The display holds the front screen exactly, padding untouched.
static int display_is(const struct screen *screen) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < STRIDE; x++) {
            unsigned want = x >= WIDTH ? UNTOUCHED
                            : screen == NULL ? BLACK
                                             : screen->pixels[y * (screen->pitch / 4) + x];
            if (shown(x, y) != want) {
                return 0;
            }
        }
    }
    return 1;
}

// The program draws into its own pixels.
static void fill(struct screen *screen, int x0, int y0, int width, int height, unsigned colour) {
    unsigned *pixels = (unsigned *)screen->pixels;
    for (int y = y0; y < y0 + height; y++) {
        for (int x = x0; x < x0 + width; x++) {
            pixels[y * (screen->pitch / 4) + x] = colour;
        }
    }
}

// The events sent to pid's screen since the last look, and the message to
// reply with; NULL for none.
static struct screen_events *taken(int pid) {
    struct screen_events *e = sent[pid];
    sent[pid] = NULL;
    return e;
}

static struct screen_request ask(int op, struct screen *screen, int sender) {
    struct screen_request request = {.op = op, .screen = screen};
    serve(&request, sender);
    return request;
}

static struct screen *open_for(int pid, const char *title) {
    unsigned *pixels = pixel_sets[pixel_sets_used++ % MAX_PIXEL_SETS];
    memset(pixels, 0, sizeof(pixel_sets[0]));
    struct screen_request request = {.op = SCREEN_OPEN, .title = title, .pixels = pixels,
                                     .width = WIDTH, .height = HEIGHT, .pitch = WIDTH * 4,
                                     .port = EVENT_PORT(pid)};
    serve(&request, pid);
    return request.result == 0 ? request.screen : NULL;
}

static void damage(struct screen *screen, int sender, int x, int y, int width, int height,
                   int want) {
    struct screen_request request = {.op = SCREEN_DAMAGE, .screen = screen, .x = x, .y = y,
                                     .width = width, .height = height};
    serve(&request, sender);
    CHECK(request.result == want, "damage %d,%d %dx%d: %d", x, y, width, height, request.result);
}

static struct input_event key(int type, int keycode, int modifiers) {
    return (struct input_event){.type = (unsigned char)type, .keycode = (unsigned short)keycode,
                                .modifiers = (unsigned short)modifiers};
}

static void test_open_and_damage(void) {
    reset_display();
    refuse_display = 1;
    CHECK(open_for(10, "refused") == NULL && screen_count == 0 && !fb_held &&
          input_held_bit == -1 && !watched[10], "no screen without the display");
    refuse_display = 0;
    CHECK(!fb_held && input_held_bit == -1, "nothing is held before the first screen");
    struct screen_request bad_open = {.op = SCREEN_OPEN, .pixels = pixel_sets[0], .width = WIDTH - 1,
                                      .height = HEIGHT, .pitch = WIDTH * 4, .port = EVENT_PORT(10)};
    serve(&bad_open, 10);
    struct screen_request no_pixels = {.op = SCREEN_OPEN, .width = WIDTH, .height = HEIGHT,
                                       .pitch = WIDTH * 4, .port = EVENT_PORT(10)};
    serve(&no_pixels, 10);
    struct screen_request no_port = {.op = SCREEN_OPEN, .pixels = pixel_sets[0], .width = WIDTH,
                                     .height = HEIGHT, .pitch = WIDTH * 4};
    serve(&no_port, 10);
    CHECK(bad_open.result == -1 && no_pixels.result == -1 && no_port.result == -1 &&
              screen_count == 0 && !fb_held,
          "a screen needs pixels as large as the display, and a port");

    struct screen *a = open_for(10, "first");
    CHECK(fb_held && input_held_bit == input_bit, "the first screen takes the display and keys");
    CHECK(a != NULL && a->id == 1 && a->owner == 10 && a->port == EVENT_PORT(10),
          "a screen for pid 10, and the port its events go to");
    struct screen_request named = {.op = SCREEN_OPEN, .title = "named", .pixels = pixel_sets[23],
                                   .width = WIDTH, .height = HEIGHT, .pitch = WIDTH * 4,
                                   .port = EVENT_PORT(11)};
    serve(&named, 11);
    CHECK(named.result == 0 && named.pid == 2 && named.id == 2,
          "the reply names the manager's pid, for the program to watch, and the screen's id");
    ask(SCREEN_CLOSE, named.screen, 11);
    CHECK(front == a && screen_count == 1, "closed again");
    CHECK(a->width == WIDTH && a->height == HEIGHT && a->pitch == WIDTH * 4 &&
              a->pixels == pixel_sets[pixel_sets_used - 1],
          "as large as the display, on the program's pixels");
    CHECK(strcmp(a->title, "first") == 0 && front == a && focus == 10 && watched[10],
          "a new screen comes to front, its program gets the keys and is watched");
    CHECK(display_is(a), "and is shown");

    fill(a, 0, 0, WIDTH, HEIGHT, RED);
    CHECK(shown(0, 0) == BLACK, "drawing shows nothing by itself");
    damage(a, 10, 2, 3, 5, 4, 0);
    CHECK(shown(2, 3) == RED && shown(6, 6) == RED && shown(7, 3) == BLACK &&
          shown(2, 7) == BLACK && shown(1, 3) == BLACK, "damage copies just its rectangle");
    damage(a, 10, -3, -2, 6, 4, 0);
    CHECK(shown(0, 0) == RED && shown(2, 1) == RED && shown(3, 0) == BLACK && shown(0, 2) == BLACK,
          "a rectangle off the top left is clipped");
    damage(a, 10, WIDTH - 2, HEIGHT - 1, 50, 50, 0);
    CHECK(shown(WIDTH - 1, HEIGHT - 1) == RED && shown(WIDTH, HEIGHT - 1) == UNTOUCHED,
          "and off the bottom right, the padding untouched");
    damage(a, 10, 5, 5, 0, 3, 0);
    damage(a, 10, WIDTH, 0, 4, 4, 0);
    CHECK(shown(5, 5) == RED && shown(WIDTH, 0) == UNTOUCHED, "empty rectangles copy nothing");
    damage(a, 99, 0, 0, WIDTH, HEIGHT, -1);
    CHECK(shown(20, 20) == BLACK, "another program's damage is refused");
    damage(a, 10, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(display_is(a), "the whole screen");

    struct screen_request bad = ask(SCREEN_DAMAGE, (struct screen *)display_pixels, 10);
    CHECK(bad.result == -1, "a screen that is not one is refused");
    bad = ask(42, a, 10);
    CHECK(bad.result == -1, "an unknown request is refused");
}

static void test_two_screens_and_keys(void) {
    struct screen *a = screens[0];
    struct screen *b = open_for(20, "second");
    CHECK(b != NULL && b->id == 3 && front == b && focus == 20 && display_is(b),
          "the second screen comes to front, black");
    fill(b, 0, 0, WIDTH, HEIGHT, BLUE);
    damage(b, 20, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(display_is(b), "it shows blue");
    fill(a, 0, 0, 4, 4, BLUE);
    damage(a, 10, 0, 0, 4, 4, 0);
    CHECK(display_is(b), "a screen behind is not copied");

    // Keys go to the front screen's program, as messages on its port.
    struct input_event meta = key(INPUT_EVENT_KEY_DOWN, KEY_LEFT_GUI, INPUT_MOD_LEFT_GUI);
    route(&meta);
    CHECK(b->head - b->tail == 1 && a->head == 0 && messages_sent == 0,
          "Meta alone waits for the front screen's program");
    send_all();
    struct screen_events *first = taken(20);
    CHECK(first != NULL && first->count == 1 && first->events[0].keycode == KEY_LEFT_GUI &&
              taken(10) == NULL,
          "and goes to it as a message on its port");

    // Meta+M: the next screen, wrapping round; the M is the manager's.
    struct input_event m_down = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_LEFT_GUI);
    struct input_event m_up = key(INPUT_EVENT_KEY_UP, KEY_M, INPUT_MOD_LEFT_GUI);
    route(&m_down);
    CHECK(front == a && focus == 10 && display_is(a), "Meta+M brings the next screen to front");
    route(&m_up);
    send_all();
    CHECK(taken(10) == NULL && taken(20) == NULL, "neither the M nor its key up is delivered");
    struct input_event meta_up = key(INPUT_EVENT_KEY_UP, KEY_LEFT_GUI, 0);
    route(&meta_up);
    send_all();
    struct screen_events *up = taken(10);
    CHECK(up != NULL && up->count == 1 && up->events[0].type == INPUT_EVENT_KEY_UP,
          "Meta's key up goes to a");
    batch_back(up);
    struct input_event right_meta_m = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_RIGHT_GUI);
    route(&right_meta_m);
    CHECK(front == b && display_is(b), "right Meta works too, and wraps round");
    route(&m_up);
    struct input_event plain_m = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_LEFT_SHIFT);
    route(&plain_m);
    send_all();
    CHECK(taken(20) == NULL && b->head - b->tail == 1,
          "M without Meta is an ordinary key; it waits while b's program has a message");
    batch_back(first);
    struct screen_events *m = taken(20);
    CHECK(m != NULL && m->count == 1 && m->events[0].keycode == KEY_M,
          "and is sent once that message is replied");

    // Many keys: a message carries SCREEN_BATCH of them, the queue holds
    // SCREEN_PENDING, and more are dropped and counted.
    for (int i = 0; i < SCREEN_PENDING + 3; i++) {
        struct input_event k = key(INPUT_EVENT_KEY_DOWN, KEY_A + i % 26, 0);
        route(&k);
    }
    CHECK(b->lost == 3, "three events lost (%u)", b->lost);
    batch_back(m);
    int read = 0, messages = 0;
    struct screen_events *e;
    while ((e = taken(20)) != NULL) {
        CHECK(e->count == SCREEN_BATCH, "full messages (%d)", e->count);
        for (int i = 0; i < e->count; i++, read++) {
            CHECK(e->events[i].keycode == KEY_A + read % 26, "event %d in order", read);
        }
        messages++;
        batch_back(e);
    }
    CHECK(read == SCREEN_PENDING && messages == SCREEN_PENDING / SCREEN_BATCH,
          "a full queue's events all arrive (%d in %d messages)", read, messages);
    struct input_event again = key(INPUT_EVENT_KEY_DOWN, KEY_ENTER, 0);
    route(&again);
    send_all();
    e = taken(20);
    CHECK(e != NULL && e->count == 1 && e->events[0].keycode == KEY_ENTER, "and it takes more");
    batch_back(e);

    // To front by request.
    struct screen_request r = ask(SCREEN_FRONT, a, 20);
    CHECK(r.result == -1 && front == b, "a program brings only its own screen to front");
    r = ask(SCREEN_FRONT, a, 10);
    CHECK(r.result == 0 && front == a && focus == 10 && display_is(a), "SCREEN_FRONT");
}

static void test_closing(void) {
    struct screen *a = screens[0], *b = screens[1];
    struct screen *c = open_for(10, "third");  // pid 10 again
    CHECK(c != NULL && front == c && screen_count == 3, "a program may open two screens");

    struct screen_request r = ask(SCREEN_CLOSE, c, 20);
    CHECK(r.result == -1 && screen_count == 3, "only its program closes a screen");
    r = ask(SCREEN_CLOSE, c, 10);
    CHECK(r.result == 0 && screen_count == 2, "closed by its program");
    CHECK(front == a && display_is(a), "the front screen closed: the first comes to front");
    CHECK(watched[10], "its program keeps its watch for its other screen");

    ask(SCREEN_FRONT, b, 20);
    ended[20] = 1;
    close_ended();
    CHECK(screen_count == 1 && screens[0] == a && front == a && display_is(a) && focus == 10,
          "a program's end closes its screen; the next one comes to front");

    struct input_event k = key(INPUT_EVENT_KEY_DOWN, KEY_A, 0);
    route(&k);
    send_all();
    struct screen_events *to_a = taken(10);
    CHECK(to_a != NULL && to_a->events[0].keycode == KEY_A, "keys go to the new front");

    // A screen that closes with its message out: the message comes back later
    // and is free again, and nothing is sent to the closed screen.
    struct screen *fourth = open_for(30, "fourth");
    route(&k);
    send_all();
    struct screen_events *to_d = taken(30);
    CHECK(to_d != NULL, "a message out to a new front");
    route(&k);
    ask(SCREEN_CLOSE, fourth, 30);
    batch_back(to_d);
    send_all();
    CHECK(taken(30) == NULL && front == a, "back after its screen closed: nothing more goes there");
    batch_back(to_a);
    int busy = 0;
    for (int i = 0; i < MAX_BATCHES; i++) {
        busy += batches[i].busy;
    }
    CHECK(busy == 0, "every message is free again (%d busy)", busy);

    // A program that ended: a message to its port fails, and is free again.
    struct screen *dead = open_for(40, "dead");
    dead_port = EVENT_PORT(40);
    route(&k);
    send_all();
    CHECK(dead->out == NULL && dead->head == dead->tail, "nothing out to an ended program");
    ended[40] = 1;
    close_ended();
    dead_port = 0;
    ended[40] = 0;
    watched[40] = 0;

    CHECK(open_for(20, "late") == NULL && screen_count == 1, "an ended program gets no screen");

    r = ask(SCREEN_CLOSE, a, 10);
    CHECK(r.result == 0 && screen_count == 0 && front == NULL && !watched[10],
          "the last screen closed: no front, the program unwatched");
    CHECK(!fb_held && input_held_bit == -1,
          "and the display and the keys are the console's again");
    route(&k);  // nobody to deliver to
    CHECK(screen_count == 0, "keys with no screen are dropped");
    struct screen *d = open_for(10, "again");
    CHECK(d != NULL && fb_held && input_held_bit == input_bit && display_is(d),
          "a new screen takes them back, the display cleared");
    ask(SCREEN_CLOSE, d, 10);

    for (int i = 0; i < MAX_SCREENS; i++) {
        CHECK(open_for(30, "many") != NULL, "screen %d", i);
    }
    CHECK(open_for(30, "too many") == NULL, "at most %d screens", MAX_SCREENS);
    ended[30] = 1;
    close_ended();
    CHECK(screen_count == 0 && front == NULL && !fb_held, "all of them close at its end");
}

static void test_lending(void) {
    struct screen *a = open_for(10, "shell");
    fill(a, 0, 0, WIDTH, HEIGHT, RED);
    damage(a, 10, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(fb_held && display_is(a), "a screen in front");

    struct screen_request r = ask(SCREEN_LEND, NULL, 40);
    CHECK(r.result == 0 && lent_to == 40 && watched[40], "pid 40 borrows the display and keys");
    CHECK(!fb_held && input_held_bit == -1, "the console has them back, for it to claim");
    r = ask(SCREEN_LEND, NULL, 41);
    CHECK(r.result == -1 && lent_to == 40, "they are lent to one program at a time");
    CHECK(open_for(41, "meanwhile") == NULL && screen_count == 1 && !watched[41],
          "no screen opens while they are lent");
    for (unsigned i = 0; i < HEIGHT * STRIDE; i++) {
        display_pixels[i] = UNTOUCHED;
    }
    damage(a, 10, 0, 0, WIDTH, HEIGHT, 0);
    struct screen_request front_request = ask(SCREEN_FRONT, a, 10);
    CHECK(front_request.result == 0 && shown(0, 0) == UNTOUCHED && shown(5, 5) == UNTOUCHED,
          "nothing is drawn on the borrower's display");
    r = ask(SCREEN_RETURN, NULL, 41);
    CHECK(r.result == -1 && lent_to == 40, "only the borrower returns them");
    r = ask(SCREEN_RETURN, NULL, 40);
    CHECK(r.result == 0 && lent_to == 0 && !watched[40], "returned, the borrower unwatched");
    CHECK(fb_held && input_held_bit == input_bit && focus == 10 && display_is(a),
          "the manager takes them back and shows the front screen again");

    // A borrower that ends: watched again, it is gone.
    CHECK(ask(SCREEN_LEND, NULL, 42).result == 0 && !fb_held, "lent again");
    ended[42] = 1;
    close_ended();
    CHECK(lent_to == 0 && fb_held && display_is(a) && screen_count == 1,
          "back at the borrower's end; the screens stay");

    // A borrower that returns them still claimed: tried again later.
    CHECK(ask(SCREEN_LEND, NULL, 43).result == 0, "lent to one that keeps its claim");
    refuse_display = 1;
    ask(SCREEN_RETURN, NULL, 43);
    CHECK(lent_to == 0 && !holding && reclaim_failed, "taking them back fails");
    refuse_display = 0;
    reclaim();
    CHECK(holding && fb_held && !reclaim_failed && display_is(a), "and works at the next wake");

    // A borrower that also has a screen keeps its watch.
    CHECK(ask(SCREEN_LEND, NULL, 10).result == 0, "the screen's own program borrows");
    ask(SCREEN_RETURN, NULL, 10);
    CHECK(watched[10] && fb_held, "and keeps its watch for its screen");

    // A borrower whose screen closes is still watched for the lend.
    struct screen *own = open_for(45, "borrower's");
    CHECK(ask(SCREEN_LEND, NULL, 45).result == 0, "a program with a screen borrows");
    ask(SCREEN_CLOSE, own, 45);
    CHECK(watched[45] && lent_to == 45, "its screen closed, it is still watched");
    ended[45] = 1;
    close_ended();
    CHECK(lent_to == 0 && fb_held && display_is(a),
          "so its end brings the display back");

    ask(SCREEN_CLOSE, a, 10);
    CHECK(ask(SCREEN_LEND, NULL, 44).result == 0 && ask(SCREEN_RETURN, NULL, 44).result == 0 &&
          !fb_held && input_held_bit == -1, "with no screen open, nothing is taken back");
}

// The display is the screen with the pointer drawn at x, y over it, clipped,
// the padding untouched.
static int display_with_pointer(const struct screen *screen, int px, int py) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < STRIDE; x++) {
            unsigned want = x >= WIDTH ? UNTOUCHED : screen->pixels[y * (screen->pitch / 4) + x];
            int col = x - px, row = y - py;
            if (x < WIDTH && col >= 0 && col < POINTER_WIDTH && row >= 0 && row < POINTER_HEIGHT &&
                pointer_shape[row][col] != ' ') {
                want = pointer_shape[row][col] == 'X' ? POINTER_EDGE : POINTER_FILL;
            }
            if (shown(x, y) != want) {
                return 0;
            }
        }
    }
    return 1;
}

static struct input_event moved_by(int dx, int dy, int buttons) {
    return (struct input_event){.type = INPUT_EVENT_MOUSE, .x = (short)dx, .y = (short)dy,
                                .buttons = (unsigned char)buttons};
}

static struct input_event moved_to(int x, int y) {
    return (struct input_event){.type = INPUT_EVENT_MOUSE, .flags = INPUT_FLAG_ABSOLUTE,
                                .x = (short)x, .y = (short)y};
}

// Every event the manager sends pid's program from now, each message replied.
static int received(int pid, struct input_event *events, int max) {
    int n = 0;
    send_all();
    struct screen_events *e;
    while ((e = taken(pid)) != NULL) {
        for (int i = 0; i < e->count && n < max; i++) {
            events[n++] = e->events[i];
        }
        batch_back(e);
    }
    return n;
}

static int is_mouse(const struct input_event *e, int x, int y, int buttons, int wheel) {
    return e->type == INPUT_EVENT_MOUSE && e->flags == 0 && e->x == x && e->y == y &&
           e->buttons == buttons && e->wheel == wheel;
}

static void test_pointer(void) {
    reset_display();
    pointer_init();
    struct screen *a = open_for(50, "pointer");
    fill(a, 0, 0, WIDTH, HEIGHT, RED);
    damage(a, 50, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(!pointer_shown && pointer_x == WIDTH / 2 && pointer_y == HEIGHT / 2 && display_is(a),
          "the pointer starts hidden, in the middle");

    struct input_event got[8];
    struct input_event e = moved_by(0, 0, 0);
    e.wheel = -1;
    route(&e);
    int n = received(50, got, 8);
    CHECK(n == 1 && is_mouse(&got[0], WIDTH / 2, HEIGHT / 2, 0, -1) && !pointer_shown &&
              display_is(a),
          "the wheel alone goes to the program at the pointer, which stays hidden");

    e = moved_by(-30, -4, 0);
    route(&e);
    CHECK(pointer_shown && pointer_x == 2 && pointer_y == 20 && display_with_pointer(a, 2, 20),
          "a move shows the pointer over the front screen");
    n = received(50, got, 8);
    CHECK(n == 1 && is_mouse(&got[0], 2, 20, 0, 0), "and the program gets its position (%d)", n);

    // Moves the program has not taken are merged; a click is not, and stays
    // where it was.
    route(&e);  // out: the message is the program's
    send_all();
    struct screen_events *out = taken(50);
    CHECK(out != NULL && pointer_x == 0 && pointer_y == 16, "clamped at the left");
    e = moved_by(1, 0, 0);
    route(&e);
    route(&e);
    e = moved_by(0, 1, 0);
    route(&e);
    CHECK(a->head - a->tail == 1 && a->pending[a->tail % SCREEN_PENDING].x == 2 &&
              a->pending[a->tail % SCREEN_PENDING].y == 17,
          "three moves wait as one, where the pointer is");
    e = moved_by(0, 0, INPUT_BUTTON_LEFT);
    route(&e);
    e = moved_by(1, 0, INPUT_BUTTON_LEFT);
    route(&e);
    route(&e);
    e = moved_by(0, 0, 0);
    route(&e);
    route(&e);  // nothing changed: nothing sent
    batch_back(out);
    n = received(50, got, 8);
    CHECK(n == 4 && is_mouse(&got[0], 2, 17, 0, 0) &&
              is_mouse(&got[1], 2, 17, INPUT_BUTTON_LEFT, 0) &&
              is_mouse(&got[2], 4, 17, INPUT_BUTTON_LEFT, 0) && is_mouse(&got[3], 4, 17, 0, 0),
          "a click where it was, a drag merged, the release (%d)", n);
    CHECK(display_with_pointer(a, 4, 17), "drawn where it went, the screen shown where it was");

    e = moved_by(1000, 1000, 0);
    route(&e);
    CHECK(pointer_x == WIDTH - 1 && pointer_y == HEIGHT - 1 &&
              display_with_pointer(a, WIDTH - 1, HEIGHT - 1),
          "clamped at the bottom right, drawn clipped, the padding untouched");
    e = moved_by(5, 3, 0);
    route(&e);
    n = received(50, got, 8);
    CHECK(n == 1 && is_mouse(&got[0], WIDTH - 1, HEIGHT - 1, 0, 0),
          "a move against the edge goes nowhere and is not sent");

    e = moved_to(INPUT_ABS_MAX / 2 + 1, 0);
    route(&e);
    CHECK(pointer_x == WIDTH / 2 && pointer_y == 0 && display_with_pointer(a, WIDTH / 2, 0),
          "a tablet's position is scaled to the display (%d,%d)", pointer_x, pointer_y);
    e = moved_to(INPUT_ABS_MAX, INPUT_ABS_MAX);
    route(&e);
    n = received(50, got, 8);
    CHECK(pointer_x == WIDTH - 1 && pointer_y == HEIGHT - 1 && n == 1 &&
              is_mouse(&got[0], WIDTH - 1, HEIGHT - 1, 0, 0),
          "its far corner is the display's, in pixels, not absolute (the two moves merged)");

    e = moved_to(INPUT_ABS_MAX / 4 + 1, INPUT_ABS_MAX / 4 + 1);
    route(&e);
    fill(a, 0, 0, WIDTH, HEIGHT, BLUE);
    damage(a, 50, 0, 0, WIDTH / 2, HEIGHT / 2, 0);
    CHECK(display_with_pointer(a, WIDTH / 4, HEIGHT / 4) == 0 && shown(0, 0) == BLUE &&
              shown(WIDTH / 4, HEIGHT / 4) == POINTER_EDGE,
          "damage under the pointer leaves it on top");
    damage(a, 50, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(display_with_pointer(a, WIDTH / 4, HEIGHT / 4), "all of it");
    received(50, got, 8);

    struct screen *b = open_for(51, "other");
    CHECK(front == b && display_with_pointer(b, WIDTH / 4, HEIGHT / 4),
          "a screen that comes to front has the pointer over it");
    e = moved_by(2, 2, 0);
    route(&e);
    CHECK(received(51, got, 8) == 1 && received(50, got, 8) == 0,
          "mouse events go to the front screen's program only");

    struct input_event shift = key(INPUT_EVENT_KEY_DOWN, KEY_LEFT_CTRL + 1, INPUT_MOD_LEFT_SHIFT);
    route(&shift);
    CHECK(pointer_shown && display_with_pointer(b, WIDTH / 4 + 2, HEIGHT / 4 + 2),
          "a modifier alone keeps it shown (Shift+click)");
    struct input_event typed_a = key(INPUT_EVENT_KEY_DOWN, KEY_A, INPUT_MOD_LEFT_SHIFT);
    route(&typed_a);
    CHECK(!pointer_shown && display_is(b), "typing hides it");
    struct input_event meta_m = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_LEFT_GUI);
    route(&meta_m);
    CHECK(front == a && display_is(a), "and it stays hidden on the next screen");
    e = moved_by(0, 0, INPUT_BUTTON_RIGHT);
    route(&e);
    CHECK(pointer_shown && display_with_pointer(a, WIDTH / 4 + 2, HEIGHT / 4 + 2),
          "a click shows it again, where it was");
    n = received(50, got, 8);
    CHECK(n == 1 && is_mouse(&got[0], WIDTH / 4 + 2, HEIGHT / 4 + 2, INPUT_BUTTON_RIGHT, 0),
          "and goes to the new front's program");
    received(51, got, 8);

    CHECK(ask(SCREEN_LEND, NULL, 52).result == 0 && !pointer_drawn,
          "lent: the console draws over it");
    for (unsigned i = 0; i < HEIGHT * STRIDE; i++) {
        display_pixels[i] = UNTOUCHED;
    }
    ask(SCREEN_RETURN, NULL, 52);
    CHECK(display_with_pointer(a, WIDTH / 4 + 2, HEIGHT / 4 + 2),
          "back: the front screen and the pointer drawn again");

    route(&typed_a);
    received(50, got, 8);
    ask(SCREEN_CLOSE, a, 50);
    ask(SCREEN_CLOSE, b, 51);
    CHECK(screen_count == 0 && !fb_held, "closed");
}

static void test_programs(void) {
    char *argv[] = {"a", "1", "+", "b", "+", "+", "nosuch", "+", "c", "x", "y", "+"};
    start_programs(12, argv, 7);
    CHECK(spawn_count == 3 && strcmp(spawned[0], "a 1") == 0 && strcmp(spawned[1], "b") == 0 &&
          strcmp(spawned[2], "c x y") == 0, "programs are separated by +, empty ones skipped");
    CHECK(spawn_flags_seen == (SPAWN_NOWAIT | SPAWN_SIGNAL(7)), "in the background, signalling");
    CHECK(program_count == 3, "a program that cannot run is not waited for");
    reaped_pid = 101;
    reap_programs();
    CHECK(program_count == 2, "an ended program is reaped");
    reaped_pid = 100;
    reap_programs();
    reaped_pid = 102;
    reap_programs();
    CHECK(program_count == 0, "all of them");
}

static void setup(void) {
    fake_set_handler(kernel);
}

static void teardown(void) {
    fake_set_handler(NULL);
}

TEST_MAIN(setup, test_open_and_damage, test_two_screens_and_keys, test_closing, test_lending,
          test_pointer, test_programs,
          teardown)
