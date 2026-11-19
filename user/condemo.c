/* condemo: two consoles of the console library (user/libconsole) in windows
 * on the framebuffer, side by side. Each is a terminal with its own escape
 * parser, grid and scrollback, drawn by the library's renderer into a
 * rectangle of the screen; the right one is larger than its window and is
 * clipped to it. The program's own output still goes to the kernel's screen
 * terminal, through stdout.
 *
 * It prints where it drew and what the left console answered to a cursor
 * position request, which the smoke test checks against a screenshot.
 */
#include "libk/koraos.h"
#include "libconsole/term.h"
#include "libconsole/term_render.h"

#define COLS 40
#define ROWS 16
#define BORDER 4
#define BORDER_COLOUR 0x00808080u

static void *console_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    return malloc(bytes);
}

/* The answers the left console gives to status requests: a real program
 * would read them like typed input. */
static char answer[32];
static size_t answer_length;

static void console_reply(void *ctx, const char *bytes, size_t length) {
    (void)ctx;
    for (size_t i = 0; i < length && answer_length < sizeof(answer) - 1; i++) {
        answer[answer_length++] = bytes[i];
    }
    answer[answer_length] = '\0';
}

static const term_host_t host = {console_alloc, console_reply, NULL};

static void feed(term_t *t, const char *s) {
    while (*s) {
        term_putc(t, *s++);
    }
}

/* A frame around a window, just outside it. */
static void frame(const struct fb_info *fb, int x, int y, int width, int height) {
    volatile uint32_t *pixels = (volatile uint32_t *)fb->addr;
    uint32_t stride = fb->pitch / 4;
    for (int py = y - BORDER; py < y + height + BORDER; py++) {
        for (int px = x - BORDER; px < x + width + BORDER; px++) {
            bool inside = px >= x && px < x + width && py >= y && py < y + height;
            if (!inside && px >= 0 && py >= 0 && (unsigned)px < fb->width &&
                (unsigned)py < fb->height) {
                pixels[(uint32_t)py * stride + (uint32_t)px] = BORDER_COLOUR;
            }
        }
    }
}

int main(void) {
    struct fb_info fb;
    if (fb_info(&fb) != 0 || fb.bpp != 32) {
        fputs("condemo: no 32-bit framebuffer\n", stderr);
        return 1;
    }
    int width = COLS * TERM_CELL_WIDTH;
    int height = ROWS * TERM_CELL_HEIGHT;
    int gap = 48;
    int left_x = ((int)fb.width - 2 * width - gap) / 2;
    int right_x = left_x + width + gap;
    int y = (int)fb.height - height - 2 * TERM_CELL_HEIGHT;  /* below the shell's lines */
    if (left_x < BORDER || y < BORDER) {
        fputs("condemo: the screen is too small\n", stderr);
        return 1;
    }

    static term_t left, right;
    if (!term_init(&left, COLS, ROWS, 100, &host) ||
        !term_init(&right, COLS + 8, ROWS + 4, 100, &host)) {
        fputs("condemo: out of memory\n", stderr);
        return 1;
    }

    /* The left console: a blue screen (erasing fills with the current
     * background), a line-drawing box, the 16 colours and a status request. */
    feed(&left, "\x1b[44m\x1b[2J\x1b[H");
    feed(&left, "\x1b[1mLeft console\x1b[22m (libconsole)\r\n\r\n");
    feed(&left, " \x1b(0lqqqqqqqqqqqqqqqqqqqqqqk\x1b(B\r\n");
    feed(&left, " \x1b(0x\x1b(B a box of DEC lines   \x1b(0x\x1b(B\r\n");
    feed(&left, " \x1b(0mqqqqqqqqqqqqqqqqqqqqqqj\x1b(B\r\n\r\n ");
    for (int i = 0; i < 16; i++) {
        char sgr[16];
        snprintf(sgr, sizeof(sgr), "\x1b[%dm  ", i < 8 ? 40 + i : 100 + i - 8);
        feed(&left, sgr);
    }
    feed(&left, "\x1b[44m\r\n\r\n \x1b[4munderlined\x1b[24m \x1b[7mreversed\x1b[27m");
    feed(&left, "\x1b[6n");

    /* The right console: red, larger than its window, and scrolled. */
    feed(&right, "\x1b[41m\x1b[2J\x1b[H");
    for (int i = 1; i <= 30; i++) {
        char line[64];
        snprintf(line, sizeof(line), "\r\nline %d of a console clipped to its window", i);
        feed(&right, line);
    }

    term_surface_t screen;
    term_surface_init(&screen, (void *)fb.addr, fb.width, fb.height, fb.pitch);

    frame(&fb, left_x, y, width, height);
    term_render_t left_view;
    term_render_init(&left_view, &left, &screen, left_x, y);
    term_render_all(&left_view);

    frame(&fb, right_x, y, width, height);
    term_surface_t window = screen;
    window.clip = (term_rect_t){right_x, y, (uint32_t)width, (uint32_t)height};
    term_render_t right_view;
    term_render_init(&right_view, &right, &window, right_x, y);
    term_render_all(&right_view);

    /* "ESC [ row ; col R" */
    int row = 0, col = 0;
    if (answer_length < 6 || answer[0] != 0x1b ||
        sscanf(answer + 1, "[%d;%dR", &row, &col) != 2) {
        fputs("condemo: no cursor report from the left console\n", stderr);
        return 1;
    }
    printf("condemo: windows %dx%d at %d,%d and %d,%d; left console cursor at %d;%d\n",
           width, height, left_x, y, right_x, y, row, col);
    return 0;
}
