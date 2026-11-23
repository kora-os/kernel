/* libscreen: the program's side of the screen manager's protocol. See
 * screen.h. */
#include "libscreen/screen.h"

/* Send a request and wait for it to come back. A request returned unhandled
 * (the manager ended) fails. */
static int transact(int manager, int reply_port, struct screen_request *r) {
    r->result = -1;
    if (port_put(manager, r, reply_port) != 0) {
        return -1;
    }
    struct port_message got;
    while (port_wait(reply_port, &got) == 1) {
        if (got.msg == r) {
            return got.kind == PORT_REPLY ? r->result : -1;
        }
    }
    return -1;
}

static int request(screen_t *s) {
    return s->gone ? -1 : transact(s->manager, s->reply_port, &s->request);
}

/* A request that needs no screen, with a reply port of its own. */
static int ask_manager(int op) {
    int manager = port_find(SCREENS_PORT);
    if (manager < 0) {
        return -1;
    }
    int reply_port = port_create(NULL);
    if (reply_port < 0) {
        return -1;
    }
    struct screen_request r = {.op = op};
    int result = transact(manager, reply_port, &r);
    port_delete(reply_port);
    return result;
}

int screens_borrow(void) {
    return ask_manager(SCREEN_LEND);
}

int screens_return(void) {
    return ask_manager(SCREEN_RETURN);
}

/* Free what screen_open made: the ports (messages still queued go back to
 * the manager), the signal, the pixels and the handle. */
static void release(screen_t *s) {
    if (s->batch != NULL) {
        port_reply(s->batch);
    }
    if (s->manager_pid > 0) {
        sig_on_end(s->manager_pid, -1);
    }
    if (s->event_port > 0) {
        port_delete(s->event_port);
    }
    if (s->reply_port > 0) {
        port_delete(s->reply_port);
    }
    if (s->bit >= 0) {
        sig_free(s->bit);
    }
    if (s->pixels != NULL) {
        free_pages(s->pixels);
    }
    free(s);
}

screen_t *screen_open(const char *title) {
    int manager = port_find(SCREENS_PORT);
    struct fb_info display;
    if (manager < 0 || fb_info(&display) != 0 || display.bpp != 32) {
        return NULL;
    }
    screen_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->manager = manager;
    s->width = display.width;
    s->height = display.height;
    s->pitch = display.width * 4;
    size_t bytes = (size_t)s->pitch * s->height;
    s->pixels = alloc_pages((bytes + KORAOS_PAGE_SIZE - 1) / KORAOS_PAGE_SIZE);  /* zeroed: black */
    s->reply_port = port_create(NULL);
    s->event_port = port_create(NULL);
    s->bit = sig_alloc(-1);
    if (s->pixels == NULL || s->reply_port < 0 || s->event_port < 0 || s->bit < 0 ||
        port_signal(s->event_port, s->bit) != 0) {
        release(s);
        return NULL;
    }
    s->request = (struct screen_request){
        .op = SCREEN_OPEN, .title = title, .pixels = s->pixels, .width = (int)s->width,
        .height = (int)s->height, .pitch = s->pitch, .port = s->event_port};
    if (transact(manager, s->reply_port, &s->request) != 0 || s->request.screen == NULL ||
        s->request.pid <= 0) {
        release(s);
        return NULL;
    }
    s->screen = s->request.screen;
    s->id = s->request.id;
    /* The manager's end sets the screen's signal too. (A reply's sender is
     * the program that sent the request: the manager says who it is.) */
    s->manager_pid = s->request.pid;
    if (sig_on_end(s->manager_pid, s->bit) != 0) {
        s->gone = 1;
    }
    return s;
}

unsigned screen_signal(const screen_t *s) {
    return SIG_MASK(s->bit);
}

int screen_event(screen_t *s, struct input_event *event) {
    for (;;) {
        if (s->batch != NULL) {
            int count = s->batch->count < SCREEN_BATCH ? s->batch->count : SCREEN_BATCH;
            if (s->next < count) {
                *event = s->batch->events[s->next++];
                return 1;
            }
            port_reply(s->batch);  /* the manager sends the next ones */
            s->batch = NULL;
        }
        struct port_message got;
        if (port_get(s->event_port, &got) != 1) {
            return 0;
        }
        if (got.kind == PORT_MESSAGE) {
            s->batch = got.msg;
            s->next = 0;
        }
    }
}

/* Watching the manager again fails once it has ended. */
int screen_gone(screen_t *s) {
    if (!s->gone && sig_on_end(s->manager_pid, s->bit) != 0) {
        s->gone = 1;
    }
    return s->gone;
}

void screen_damage(screen_t *s, int x, int y, int width, int height) {
    s->request = (struct screen_request){
        .op = SCREEN_DAMAGE, .screen = s->screen, .x = x, .y = y, .width = width, .height = height};
    request(s);
}

void screen_to_front(screen_t *s) {
    s->request = (struct screen_request){.op = SCREEN_FRONT, .screen = s->screen};
    request(s);
}

void screen_close(screen_t *s) {
    /* Once the manager has replied (or ended), it no longer reads the pixels. */
    s->request = (struct screen_request){.op = SCREEN_CLOSE, .screen = s->screen};
    request(s);
    release(s);
}
