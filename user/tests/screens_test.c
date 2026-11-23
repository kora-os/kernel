// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of the screen manager (user/screens.c) on the fake kernel, with
// libscreen's reading side: its requests, what it copies to the display (host
// memory with padding that must stay untouched), Meta+M, where the keys go,
// a full event ring, screens closed by their programs or by their ends, and
// how it starts its programs.

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
static int last_signal_tid;
static unsigned last_signal_mask, signals_sent;
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
    case SYS_INPUT_FOCUS:
        focus = (int)a0;
        return 0;
    case SYS_SIG_SEND:
        last_signal_tid = (int)a0;
        last_signal_mask = (unsigned)a1;
        signals_sent++;
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

static void fill(struct screen *screen, int x0, int y0, int width, int height, unsigned colour) {
    for (int y = y0; y < y0 + height; y++) {
        for (int x = x0; x < x0 + width; x++) {
            screen->pixels[y * (screen->pitch / 4) + x] = colour;
        }
    }
}

static struct screen_request ask(int op, struct screen *screen, int sender) {
    struct screen_request request = {.op = op, .screen = screen};
    serve(&request, sender);
    return request;
}

static struct screen *open_for(int pid, const char *title) {
    struct screen_request request = {.op = SCREEN_OPEN, .tid = pid + 1, .bit = 9, .title = title};
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

    struct screen *a = open_for(10, "first");
    CHECK(fb_held && input_held_bit == input_bit, "the first screen takes the display and keys");
    CHECK(a != NULL && a->id == 1 && a->owner == 10 && a->owner_tid == 11 && a->owner_bit == 9,
          "a screen for pid 10, its thread and its bit");
    CHECK(a->width == WIDTH && a->height == HEIGHT && a->pitch == WIDTH * 4,
          "as large as the display, rows without padding");
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
    CHECK(b != NULL && b->id == 2 && front == b && focus == 20 && display_is(b),
          "the second screen comes to front, black");
    fill(b, 0, 0, WIDTH, HEIGHT, BLUE);
    damage(b, 20, 0, 0, WIDTH, HEIGHT, 0);
    CHECK(display_is(b), "it shows blue");
    fill(a, 0, 0, 4, 4, BLUE);
    damage(a, 10, 0, 0, 4, 4, 0);
    CHECK(display_is(b), "a screen behind is not copied");

    // Keys go to the front screen's program, which is signalled.
    signals_sent = 0;
    struct input_event meta = key(INPUT_EVENT_KEY_DOWN, KEY_LEFT_GUI, INPUT_MOD_LEFT_GUI);
    route(&meta);
    CHECK(b->head == 1 && b->events[0].keycode == KEY_LEFT_GUI && a->head == 0,
          "Meta alone goes to the front screen");
    CHECK(signals_sent == 1 && last_signal_tid == 21 && last_signal_mask == SIG_MASK(9),
          "its thread is signalled on its bit");

    // Meta+M: the next screen, wrapping round; the M is the manager's.
    struct input_event m_down = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_LEFT_GUI);
    struct input_event m_up = key(INPUT_EVENT_KEY_UP, KEY_M, INPUT_MOD_LEFT_GUI);
    route(&m_down);
    CHECK(front == a && focus == 10 && display_is(a), "Meta+M brings the next screen to front");
    route(&m_up);
    CHECK(a->head == 0 && b->head == 1, "neither the M nor its key up is delivered");
    struct input_event meta_up = key(INPUT_EVENT_KEY_UP, KEY_LEFT_GUI, 0);
    route(&meta_up);
    CHECK(a->head == 1 && a->events[0].type == INPUT_EVENT_KEY_UP, "Meta's key up goes to a");
    struct input_event right_meta_m = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_RIGHT_GUI);
    route(&right_meta_m);
    CHECK(front == b && display_is(b), "right Meta works too, and wraps round");
    route(&m_up);
    struct input_event plain_m = key(INPUT_EVENT_KEY_DOWN, KEY_M, INPUT_MOD_LEFT_SHIFT);
    route(&plain_m);
    CHECK(front == b && b->head == 2 && b->events[1].keycode == KEY_M,
          "M without Meta is an ordinary key");

    // The program's side reads them in order.
    screen_t handle = {.screen = b};
    struct input_event got;
    CHECK(screen_event(&handle, &got) && got.keycode == KEY_LEFT_GUI && screen_event(&handle, &got) &&
          got.keycode == KEY_M && !screen_event(&handle, &got), "libscreen reads them in order");

    // A full ring drops the newest and counts them.
    for (int i = 0; i < SCREEN_EVENTS + 3; i++) {
        struct input_event k = key(INPUT_EVENT_KEY_DOWN, KEY_A + i % 26, 0);
        route(&k);
    }
    CHECK(b->lost == 3, "three events lost (%u)", b->lost);
    int read = 0;
    while (screen_event(&handle, &got)) {
        CHECK(got.keycode == KEY_A + read % 26, "event %d in order", read);
        read++;
    }
    CHECK(read == SCREEN_EVENTS, "a full ring's events are all read (%d)", read);
    struct input_event again = key(INPUT_EVENT_KEY_DOWN, KEY_ENTER, 0);
    route(&again);
    CHECK(screen_event(&handle, &got) && got.keycode == KEY_ENTER, "and it takes more");

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
    unsigned head = a->head;
    route(&k);
    CHECK(a->head == head + 1, "keys go to the new front");

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
          test_programs,
          teardown)
