/* condemo: consoles of the console library (user/libconsole) in windows on
 * the framebuffer.
 *
 *   condemo                  two consoles side by side. Each is a terminal
 *                            with its own escape parser, grid and scrollback,
 *                            drawn by the library's renderer into a rectangle
 *                            of the screen; the right one is larger than its
 *                            window and is clipped to it. It prints where it
 *                            drew and what the left console answered to a
 *                            cursor position request.
 *   condemo <program> [args] runs the program with its stdout and stderr in a
 *                            console window: condemo is their console
 *                            handler, serving the packets the program's libc
 *                            sends to its port (<_libc/console.h>). stdin
 *                            stays condemo's own. It prints how the program
 *                            ended and how many bytes it showed.
 *
 * Under the screen manager (screens) condemo draws on a screen of its own,
 * which it closes on Escape; otherwise straight into the framebuffer, over
 * the kernel's screen terminal.
 *
 * condemo's own output still goes to its stdout: the kernel's screen terminal.
 * The smoke test checks both against screenshots.
 */
#include "libk/koraos.h"
#include "libconsole/term.h"
#include "libconsole/term_render.h"
#include "libscreen/screen.h"

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

static const term_host_t host = {console_alloc, console_reply, NULL, NULL};

/* condemo's screen under the screen manager, or NULL. */
static screen_t *screen;

/* What was drawn in a rectangle: on a screen, tell the manager. */
static void show(int x, int y, int width, int height) {
    if (screen != NULL) {
        screen_damage(screen, x, y, width, height);
    }
}

/* On a screen, keep it until Escape, then close it. */
static void wait_for_escape(void) {
    if (screen == NULL) {
        return;
    }
    printf("condemo: Escape closes screen %d\n", screen->id);
    fflush(stdout);
    for (;;) {
        sig_wait(screen_signal(screen));
        struct input_event event;
        while (screen_event(screen, &event)) {
            if (event.type == INPUT_EVENT_KEY_DOWN && event.keycode == KEY_ESCAPE) {
                screen_close(screen);
                screen = NULL;
                return;
            }
        }
        if (screen_gone(screen)) {  /* the manager ended */
            screen_close(screen);
            screen = NULL;
            return;
        }
    }
}

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

/* The window of `condemo <program>`: one console, 80 columns wide. */
#define RUN_COLS 80
#define RUN_ROWS 16

/* What a WRITE packet's bytes do to the console: the program's '\n' starts a
 * new line, as the kernel's console does it (ONLCR). Reads are refused: the
 * program's stdin is not this console. */
static long serve(term_t *t, struct con_packet *packet) {
    if (packet->op != CON_WRITE) {
        return -1;
    }
    const char *bytes = packet->buf;
    for (unsigned long i = 0; i < packet->length; i++) {
        if (bytes[i] == '\n') {
            term_putc(t, '\r');
        }
        term_putc(t, bytes[i]);
    }
    return (long)packet->length;
}

static int run_in_window(const struct fb_info *fb, int argc, char **argv) {
    int width = RUN_COLS * TERM_CELL_WIDTH;
    int height = RUN_ROWS * TERM_CELL_HEIGHT;
    int x = ((int)fb->width - width) / 2;
    int y = (int)fb->height - height - 2 * TERM_CELL_HEIGHT;
    if (x < BORDER || y < BORDER) {
        fputs("condemo: the screen is too small\n", stderr);
        return 1;
    }
    static term_t console;
    if (!term_init(&console, RUN_COLS, RUN_ROWS, 100, &host)) {
        fputs("condemo: out of memory\n", stderr);
        return 1;
    }
    feed(&console, "\x1b[44m\x1b[2J\x1b[H");  /* a blue window */

    /* The program gets condemo's own stdin, and this console for the rest.
     * Two signals wake condemo: a packet on its port, and the program's end. */
    struct std_handle std[3];
    int port = port_create(NULL);
    int port_bit = sig_alloc(-1);
    int end_bit = sig_alloc(-1);
    if (port < 0 || port_bit < 0 || end_bit < 0 || port_signal(port, port_bit) < 0 ||
        std_handles(std) < 0) {
        fputs("condemo: no port\n", stderr);
        return 1;
    }
    std[1] = std[2] = (struct std_handle){STD_PORT, port};
    int pid = spawn_std(argv[0], argc, argv, SPAWN_NOWAIT | SPAWN_SIGNAL(end_bit), std);
    if (pid < 0) {
        fprintf(stderr, "condemo: cannot run %s\n", argv[0]);
        port_delete(port);
        return 1;
    }

    term_surface_t surface;
    term_surface_init(&surface, (void *)fb->addr, fb->width, fb->height, fb->pitch);
    frame(fb, x, y, width, height);
    term_render_t view;
    term_render_init(&view, &console, &surface, x, y);
    term_render_all(&view);
    show(x - BORDER, y - BORDER, width + 2 * BORDER, height + 2 * BORDER);

    /* Serve its packets until it has ended. Each write waits for its reply,
     * so once the program is gone nothing of it is left to serve. */
    unsigned long shown = 0;
    int code = -1;
    for (;;) {
        unsigned signals = sig_wait(SIG_MASK(port_bit) | SIG_MASK(end_bit));
        struct port_message got;
        while (port_get(port, &got) == 1) {
            if (got.kind != PORT_MESSAGE) {
                continue;
            }
            struct con_packet *packet = got.msg;
            packet->result = serve(&console, packet);
            if (packet->result > 0) {
                shown += (unsigned long)packet->result;
            }
            port_reply(packet);
        }
        term_render_flush(&view);
        show(x, y, width, height);
        if ((signals & SIG_MASK(end_bit)) && waitpid(pid, &code, WNOHANG) == pid) {
            break;
        }
    }
    port_delete(port);
    sig_free(port_bit);
    sig_free(end_bit);
    printf("condemo: %s exited with %d; %lu bytes in its window\n", argv[0], code, shown);
    return 0;
}

int main(int argc, char **argv) {
    struct fb_info fb;
    screen = screen_open("condemo");
    if (screen != NULL) {
        fb = (struct fb_info){(unsigned long)screen->pixels, screen->width, screen->height,
                              screen->pitch, 32};
    } else if (fb_info(&fb) != 0 || fb.bpp != 32) {
        fputs("condemo: no 32-bit framebuffer\n", stderr);
        return 1;
    }
    if (argc > 1) {
        int code = run_in_window(&fb, argc - 1, argv + 1);
        wait_for_escape();
        return code;
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

    term_surface_t surface;
    term_surface_init(&surface, (void *)fb.addr, fb.width, fb.height, fb.pitch);

    frame(&fb, left_x, y, width, height);
    term_render_t left_view;
    term_render_init(&left_view, &left, &surface, left_x, y);
    term_render_all(&left_view);

    frame(&fb, right_x, y, width, height);
    term_surface_t window = surface;
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
    show(0, 0, (int)fb.width, (int)fb.height);
    printf("condemo: windows %dx%d at %d,%d and %d,%d; left console cursor at %d;%d\n",
           width, height, left_x, y, right_x, y, row, col);
    wait_for_escape();
    return 0;
}
