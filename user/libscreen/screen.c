/* libscreen: the program's side of the screen manager's protocol. See
 * screen.h. */
#include "libscreen/screen.h"

/* Send the handle's request and wait for it to come back. A request returned
 * unhandled (the manager ended) fails. */
static int request(screen_t *s) {
    s->request.result = -1;
    if (port_put(s->manager, &s->request, s->reply_port) != 0) {
        return -1;
    }
    struct port_message got;
    while (port_wait(s->reply_port, &got) == 1) {
        if (got.msg == &s->request) {
            return got.kind == PORT_REPLY ? s->request.result : -1;
        }
    }
    return -1;
}

screen_t *screen_open(const char *title) {
    int manager = port_find(SCREENS_PORT);
    if (manager < 0) {
        return NULL;
    }
    screen_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    s->manager = manager;
    s->reply_port = port_create(NULL);
    s->bit = sig_alloc(-1);
    if (s->reply_port < 0 || s->bit < 0) {
        goto fail;
    }
    s->request = (struct screen_request){
        .op = SCREEN_OPEN, .tid = gettid(), .bit = s->bit, .title = title};
    if (request(s) != 0 || s->request.screen == NULL) {
        goto fail;
    }
    struct screen *screen = s->request.screen;
    s->screen = screen;
    s->width = screen->width;
    s->height = screen->height;
    s->pitch = screen->pitch;
    s->pixels = screen->pixels;
    s->id = screen->id;
    return s;

fail:
    if (s->reply_port >= 0) {
        port_delete(s->reply_port);
    }
    if (s->bit >= 0) {
        sig_free(s->bit);
    }
    free(s);
    return NULL;
}

unsigned screen_signal(const screen_t *s) {
    return SIG_MASK(s->bit);
}

int screen_event(screen_t *s, struct input_event *event) {
    struct screen *screen = s->screen;
    unsigned tail = screen->tail;
    if (tail == __atomic_load_n(&screen->head, __ATOMIC_ACQUIRE)) {
        return 0;
    }
    *event = screen->events[tail % SCREEN_EVENTS];
    __atomic_store_n(&screen->tail, tail + 1, __ATOMIC_RELEASE);
    return 1;
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
    s->request = (struct screen_request){.op = SCREEN_CLOSE, .screen = s->screen};
    request(s);
    port_delete(s->reply_port);
    sig_free(s->bit);
    free(s);
}
