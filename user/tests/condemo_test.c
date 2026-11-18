// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test of condemo on the fake kernel, with the console library built as
// user programs build it (the libc's ISO headers, not the kernel's). The test
// stands in for the framebuffer, host memory filled with a marker, and checks
// what the two consoles drew, where they drew it, and what they left alone.

#include "test.h"

#include <stdio.h>
#include <string.h>

#include "fake_syscall.h"

#define main condemo_main
#include "../condemo.c"
#undef main

#define FB_WIDTH 1024
#define FB_HEIGHT 768
#define FB_STRIDE 1030  // wider than the picture: the padding must stay untouched
#define UNTOUCHED 0x55555555u
static unsigned framebuffer[FB_HEIGHT * FB_STRIDE];
static unsigned fb_width, fb_bpp;
static int have_framebuffer;

static long kernel(long number, long a0, long a1, long a2, long a3) {
    (void)a1;
    (void)a2;
    (void)a3;
    if (number == SYS_FB_INFO) {
        if (!have_framebuffer) {
            return -1;
        }
        struct fb_info *fb = (struct fb_info *)a0;
        fb->addr = (unsigned long)framebuffer;
        fb->width = fb_width;
        fb->height = FB_HEIGHT;
        fb->pitch = FB_STRIDE * 4;
        fb->bpp = fb_bpp;
        return 0;
    }
    return -1;
}

static void reset(void) {
    fflush(stdout);
    fake_reset_console();
    fake_reset_files();
    fake_set_handler(kernel);
    for (unsigned i = 0; i < FB_HEIGHT * FB_STRIDE; i++) {
        framebuffer[i] = UNTOUCHED;
    }
    have_framebuffer = 1;
    fb_width = FB_WIDTH;
    fb_bpp = 32;
}

static int run(void) {
    int status = condemo_main();
    fflush(stdout);
    return status;
}

static int printed(const char *text, const char *errors) {
    return strcmp(fake_output(1, NULL), text) == 0 && strcmp(fake_output(2, NULL), errors) == 0;
}

static unsigned pixel(unsigned x, unsigned y) {
    return framebuffer[y * FB_STRIDE + x];
}

// Where condemo puts its windows on a 1024x768 screen: centred, 48 pixels
// apart, two cell rows above the bottom.
#define LEFT_X 168
#define RIGHT_X 536
#define TOP 624
#define WIDTH (COLS * 8)
#define HEIGHT (ROWS * 8)

// Pixel (dx, dy) of cell (row, col) of the left console.
static unsigned left_cell(unsigned row, unsigned col, unsigned dx, unsigned dy) {
    return pixel(LEFT_X + col * 8 + dx, TOP + row * 8 + dy);
}

#define BLUE 0x000000eeu   // SGR 44
#define RED 0x00cd0000u    // SGR 41
#define WHITE 0x00ffffffu  // the default foreground

static void test_failures_reported(void) {
    reset();
    have_framebuffer = 0;
    CHECK(run() == 1 && printed("", "condemo: no 32-bit framebuffer\n"), "no framebuffer");
    reset();
    fb_bpp = 16;
    CHECK(run() == 1 && printed("", "condemo: no 32-bit framebuffer\n"), "a 16-bit framebuffer");
    reset();
    fb_width = 600;
    CHECK(run() == 1 && printed("", "condemo: the screen is too small\n"), "a narrow screen");
    CHECK(pixel(0, 0) == UNTOUCHED && pixel(599, FB_HEIGHT - 1) == UNTOUCHED,
          "nothing is drawn when the windows do not fit");
}

static void test_two_consoles(void) {
    reset();
    CHECK(run() == 0 &&
              printed("condemo: windows 320x128 at 168,624 and 536,624; "
                      "left console cursor at 9;21\n", ""),
          "condemo reports its windows and the left console's cursor report (%s / %s)",
          fake_output(1, NULL), fake_output(2, NULL));

    // The left console: blue where nothing is written, a white line through
    // the middle of the box's top edge (row 2, DEC 'q'), the 16 colours from
    // column 1 of row 6, two cells each, and a block cursor at row 8, col 20.
    CHECK(left_cell(15, 39, 7, 7) == BLUE && left_cell(1, 30, 0, 0) == BLUE,
          "erasing filled the left console with its blue background");
    CHECK(left_cell(2, 5, 2, 3) == WHITE && left_cell(2, 5, 2, 4) == WHITE &&
              left_cell(2, 5, 2, 0) == BLUE && left_cell(2, 5, 2, 7) == BLUE,
          "a DEC line-drawing line runs through the middle of its cells");
    CHECK(left_cell(3, 1, 3, 0) == WHITE && left_cell(3, 24, 3, 0) == WHITE &&
              left_cell(3, 25, 3, 0) == BLUE && left_cell(2, 24, 3, 7) == WHITE &&
              left_cell(4, 24, 3, 0) == WHITE,
          "the box's sides line up with its corners, in columns 1 and 24");
    static const unsigned colours[16] = {
        0x000000, 0xcd0000, 0x00cd00, 0xcdcd00, 0x0000ee, 0xcd00cd, 0x00cdcd, 0xe5e5e5,
        0x7f7f7f, 0xff0000, 0x00ff00, 0xffff00, 0x5c5cff, 0xff00ff, 0x00ffff, 0xffffff};
    for (unsigned i = 0; i < 16; i++) {
        CHECK(left_cell(6, 1 + 2 * i, 0, 0) == colours[i] && left_cell(6, 2 + 2 * i, 7, 7) == colours[i],
              "colour %u is drawn in its two cells (0x%06x)", i, left_cell(6, 1 + 2 * i, 0, 0));
    }
    CHECK(left_cell(8, 20, 3, 3) == WHITE && left_cell(8, 21, 3, 3) == BLUE,
          "the block cursor is drawn where the console reported it");
    CHECK(left_cell(8, 1, 0, 7) == WHITE, "underlined text has its bottom row set");
    CHECK(left_cell(8, 12, 0, 0) == WHITE, "reversed text swaps the colours");

    // The right console is 48 by 20 cells in a 40 by 16 window: it fills the
    // window and stops at its edge.
    unsigned red = 0, untouched = 0;
    for (unsigned y = TOP; y < TOP + HEIGHT; y++) {
        for (unsigned x = RIGHT_X; x < RIGHT_X + WIDTH; x++) {
            red += pixel(x, y) == RED;
            untouched += pixel(x, y) == UNTOUCHED;
        }
    }
    CHECK(untouched == 0 && red > WIDTH * HEIGHT / 2,
          "the right console covers its window (%u red, %u untouched)", red, untouched);
    // Unclipped, it would reach 64 pixels further right, and below the
    // window to the bottom of the screen.
    int clipped = 1;
    for (unsigned y = TOP - BORDER; y < FB_HEIGHT; y++) {
        for (unsigned x = RIGHT_X - BORDER; x < RIGHT_X + 48 * 8 + 8; x++) {
            bool framed = x < RIGHT_X + WIDTH + BORDER && y < TOP + HEIGHT + BORDER;
            clipped &= framed || pixel(x, y) == UNTOUCHED;
        }
    }
    CHECK(clipped, "nothing of the right console is drawn past its window's frame");

    // The frames, and nothing else.
    CHECK(pixel(LEFT_X - 1, TOP) == BORDER_COLOUR && pixel(RIGHT_X + WIDTH, TOP + HEIGHT) == BORDER_COLOUR &&
              pixel(LEFT_X - BORDER - 1, TOP) == UNTOUCHED && pixel(LEFT_X, TOP - BORDER - 1) == UNTOUCHED,
          "a frame of %d pixels surrounds each window", BORDER);
    unsigned drawn = 0, padding = 1;
    for (unsigned y = 0; y < FB_HEIGHT; y++) {
        for (unsigned x = 0; x < FB_WIDTH; x++) {
            drawn += pixel(x, y) != UNTOUCHED;
        }
        for (unsigned x = FB_WIDTH; x < FB_STRIDE; x++) {
            padding &= pixel(x, y) == UNTOUCHED;
        }
    }
    unsigned framed = 2 * (WIDTH + 2 * BORDER) * (HEIGHT + 2 * BORDER);
    CHECK(drawn == framed, "only the windows and their frames are drawn (%u of %u pixels)",
          drawn, framed);
    CHECK(padding, "the row padding past the width is left alone");
}

TEST_MAIN(test_failures_reported, test_two_consoles)
