// SPDX-License-Identifier: GPL-3.0-or-later
//
// The kernel's framebuffer console: libconsole's terminal and renderer on the
// screen, and the screen_* calls the tty uses. See console_fb.h.

#include "video/console_fb.h"

#include "mm.h"
#include "mm/frame_alloc.h"
#include "tty.h"

// The terminal's buffers come from the frame allocator, and are never freed.
static void *console_alloc(void *ctx, size_t bytes) {
    (void)ctx;
    return frame_alloc_pages((bytes + PAGE_SIZE - 1) / PAGE_SIZE);
}

// Status reports go back to the programs reading the console, as if typed.
static void console_reply(void *ctx, const char *bytes, size_t length) {
    (void)ctx;
    for (size_t i = 0; i < length; i++) {
        tty_input_push(bytes[i]);
    }
}

static const term_host_t console_host = {console_alloc, console_reply, NULL};

void fb_console_flush(fb_console_t *console) {
    if (console) {
        term_render_flush(&console->render);
    }
}

int fb_console_init(fb_console_t *console, uint32_t width, uint32_t height, uint32_t depth) {
    if (!console || depth != 32) {
        return 0;  // the renderer writes 32 bpp pixels
    }
    if (!framebuffer_init(&console->fb, width, height, depth)) {
        return 0;
    }
    if (!term_init(&console->term, console->fb.width / TERM_CELL_WIDTH,
                   console->fb.height / TERM_CELL_HEIGHT, TERM_SCROLLBACK_LINES,
                   &console_host)) {
        return 0;
    }
    term_surface_t screen;
    term_surface_init(&screen, console->fb.buffer, console->fb.width, console->fb.height,
                      console->fb.pitch);
    term_render_init(&console->render, &console->term, &screen, 0, 0);
    framebuffer_clear(&console->fb, TERM_DEFAULT_BG);  // incl. any partial cell
    fb_console_flush(console);
    return 1;
}

void fb_console_write(fb_console_t *console, const char *text) {
    if (!console || !text) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            term_putc(&console->term, '\r');
        }
        term_putc(&console->term, *text++);
    }
    fb_console_flush(console);
}

// The console the kernel and syscalls treat as "the screen".
static fb_console_t *active_console;

// The process the screen is handed to (screen_claim), or 0: while it holds
// it the terminal keeps its text but draws nothing. Under the BKL.
static int screen_owner_pid;

void fb_console_make_active(fb_console_t *console) {
    active_console = console;
}

void screen_putc(char c) {
    if (active_console) {
        term_putc(&active_console->term, c);
    }
}

void screen_flush(void) {
    if (screen_owner_pid == 0) {
        fb_console_flush(active_console);
    }
}

void screen_scroll_view(int halfpages) {
    if (active_console) {
        term_t *t = &active_console->term;
        term_scroll_view(t, halfpages * (int)(t->rows / 2));
    }
}

const framebuffer_info_t *screen_framebuffer(void) {
    return active_console ? &active_console->fb : NULL;
}

int screen_claim(int pid) {
    if (active_console == NULL || pid <= 0 ||
        (screen_owner_pid != 0 && screen_owner_pid != pid)) {
        return -1;
    }
    if (screen_owner_pid == 0) {
        fb_console_flush(active_console);  // what was printed is in the grid, drawn
    }
    screen_owner_pid = pid;
    return 0;
}

void screen_release(int pid) {
    if (pid == 0 || screen_owner_pid != pid) {
        return;
    }
    screen_owner_pid = 0;
    // Whatever the owner drew goes: the terminal redraws all of it, with the
    // text printed meanwhile.
    framebuffer_clear(&active_console->fb, TERM_DEFAULT_BG);
    term_render_all(&active_console->render);
}

int screen_owner(void) {
    return screen_owner_pid;
}
