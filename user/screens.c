/* screens: the screen manager, Amiga style (project_doc/console-plan.md).
 *
 *   screens program [args] [+ program [args]]...
 *
 * Starts the programs and serves screens to any program that opens one (libscreen,
 * <libscreen/screen.h>): full-size pixel buffers in the program's memory,
 * which screens only reads. The one in front is copied to the display as its
 * program reports what changed, and gets the keys, as messages from screens'
 * memory on the screen's port. So neither side writes into the other's
 * memory, and either may end, crashed or not. Meta+M (Command or the Windows key) brings the next screen
 * to front; a newly opened screen comes to front by itself.
 *
 * While a screen is open, screens holds the display and the keyboard, taken
 * from the kernel's console; with none, the console has them back, so a
 * program that opens no screen works as it would without screens. A program
 * may also borrow them (screens_borrow): screens gives them back to the
 * console until the program returns them or ends, so it can claim them
 * itself, as a program that drives the hardware does without screens.
 * A screen closes when its program closes it or ends, crashed or not. When
 * the last screen has closed, the programs it started have ended and nothing
 * is lent, screens ends. It reports what it does on its stdout, the kernel
 * console, which shows it then.
 */
#include "libk/koraos.h"
#include "libscreen/screen.h"

#define MAX_SCREENS 16
#define MAX_PROGRAMS 8
#define BLACK 0x00000000u
#define SCREEN_PENDING 256      /* events waiting for a screen's program: a power of two */
#define MAX_BATCHES (2 * MAX_SCREENS)

/* A screen, the manager's own record of it; programs name it by its address. */
struct screen {
    int id;                 /* 1, 2, ... in the order they were opened */
    int owner;              /* the pid of the program that opened it */
    int port;               /* the program's port its events go to */
    unsigned width, height;
    unsigned pitch;         /* bytes a row */
    const unsigned *pixels; /* the program's memory: only read */
    char title[SCREEN_TITLE_MAX];
    /* Events not sent yet, oldest first; more are dropped and counted. */
    struct input_event pending[SCREEN_PENDING];
    unsigned head, tail;
    unsigned lost;
    struct batch *out;      /* the message the program has, or NULL */
};

/* A message of events. It stays the manager's until the program replies (or
 * the kernel returns it), even after its screen has closed. */
struct batch {
    struct screen_events events;  /* first: the message's address */
    struct screen *screen;        /* whose, or NULL once that screen closed */
    int busy;
};
static struct batch batches[MAX_BATCHES];
static int reply_port;      /* the batches come back here */

static struct fb_info display;
static struct screen *screens[MAX_SCREENS];  /* in the order they were opened */
static int screen_count;
static struct screen *front;
static int next_id = 1;
static int end_bit;          /* set when a screen's program ends */
static int input_bit;        /* set when a key event is queued for us */
static int holding;          /* the display and the keyboard are ours */
static int meta_m_held;      /* an M swallowed as Meta+M: swallow its key up */
static int lent_to;          /* the pid the display and the keyboard are lent to */
static int reclaim_failed;   /* taking them back failed, and was reported */

static int programs[MAX_PROGRAMS];  /* the children still running */
static int program_count;

/* --- The display --------------------------------------------------------------- */

/* Copy a rectangle of the front screen to the display, clipped to it. */
static void copy_rect(int x, int y, int width, int height) {
    if (front == NULL || !holding) {
        return;
    }
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (width > (int)front->width - x) {
        width = (int)front->width - x;
    }
    if (height > (int)front->height - y) {
        height = (int)front->height - y;
    }
    if (width <= 0 || height <= 0) {
        return;
    }
    for (int row = y; row < y + height; row++) {
        const unsigned *from = (const unsigned *)((const char *)front->pixels +
                                                  (unsigned long)row * front->pitch) + x;
        unsigned *to = (unsigned *)((char *)display.addr + (unsigned long)row * display.pitch) + x;
        memcpy(to, from, (size_t)width * 4);
    }
}

static void clear_display(void) {
    for (unsigned row = 0; row < display.height; row++) {
        unsigned *to = (unsigned *)((char *)display.addr + (unsigned long)row * display.pitch);
        for (unsigned x = 0; x < display.width; x++) {
            to[x] = BLACK;
        }
    }
}

static void to_front(struct screen *screen) {
    front = screen;
    if (holding) {
        copy_rect(0, 0, (int)screen->width, (int)screen->height);
        input_focus(screen->owner);
    }
    printf("screens: screen %d in front (%s)\n", screen->id, screen->title);
}

/* Take the display and the keyboard from the console (the first screen
 * opens), or give them back (the last one closed: the console redraws). */
static int hold(int on) {
    if (on) {
        if (fb_claim(1) != 0) {
            return -1;
        }
        if (input_claim(input_bit) != 0) {
            fb_claim(0);
            return -1;
        }
        clear_display();
    } else {
        input_claim(-1);
        fb_claim(0);
        meta_m_held = 0;
    }
    holding = on;
    return 0;
}

/* --- Screens ------------------------------------------------------------------- */

static int index_of(const struct screen *screen) {
    for (int i = 0; i < screen_count; i++) {
        if (screens[i] == screen) {
            return i;
        }
    }
    return -1;
}

/* Stop watching a program that has no screen left and borrows nothing. */
static void unwatch(int owner) {
    if (owner == lent_to) {
        return;
    }
    for (int i = 0; i < screen_count; i++) {
        if (screens[i]->owner == owner) {
            return;
        }
    }
    sig_on_end(owner, -1);
}

static struct screen *open_screen(int owner, const struct screen_request *request) {
    /* Watch its program, which may have ended since it asked. */
    if (screen_count == MAX_SCREENS || lent_to != 0 || request->pixels == NULL ||
        request->width != (int)display.width || request->height != (int)display.height ||
        request->pitch < display.width * 4 || request->port <= 0 ||
        sig_on_end(owner, end_bit) != 0) {
        return NULL;
    }
    if (!holding && hold(1) != 0) {
        printf("screens: the display or the keyboard is taken\n");
        unwatch(owner);
        return NULL;
    }
    struct screen *screen = calloc(1, sizeof(*screen));
    if (screen == NULL) {
        unwatch(owner);
        if (screen_count == 0 && holding) {
            hold(0);
        }
        return NULL;
    }
    screen->id = next_id++;
    screen->owner = owner;
    screen->port = request->port;
    screen->width = display.width;
    screen->height = display.height;
    screen->pitch = request->pitch;
    screen->pixels = request->pixels;
    snprintf(screen->title, sizeof(screen->title), "%s", request->title != NULL ? request->title : "");
    screens[screen_count++] = screen;
    printf("screens: screen %d open for %s (pid %d)\n", screen->id, screen->title, owner);
    to_front(screen);
    return screen;
}

static void close_screen(struct screen *screen, const char *why) {
    int i = index_of(screen);
    if (i < 0) {
        return;
    }
    for (int j = i; j < screen_count - 1; j++) {
        screens[j] = screens[j + 1];
    }
    screen_count--;
    printf("screens: screen %d closed (%s), %s\n", screen->id, screen->title, why);
    if (screen_count == 0) {
        front = NULL;
        if (holding) {
            hold(0);
        }
    } else if (screen == front) {
        /* The next one opened after it, or the first. */
        to_front(screens[i < screen_count ? i : 0]);
    }
    unwatch(screen->owner);
    if (screen->out != NULL) {
        screen->out->screen = NULL;  /* it comes back later, to an empty pool slot */
    }
    free(screen);
}

/* --- Lending ------------------------------------------------------------------ */

/* Take the display and the keyboard back once nothing is lent and a screen
 * is open, and show the front screen. A borrower that returned them still
 * claimed keeps them: then this is tried again at the manager's next wake. */
static void reclaim(void) {
    if (holding || lent_to != 0 || screen_count == 0) {
        return;
    }
    if (hold(1) != 0) {
        if (!reclaim_failed) {
            printf("screens: the display or the keyboard is still taken\n");
        }
        reclaim_failed = 1;
        return;
    }
    reclaim_failed = 0;
    to_front(front);
}

static int lend(int borrower) {
    if (lent_to != 0 || sig_on_end(borrower, end_bit) != 0) {
        return -1;
    }
    if (holding) {
        hold(0);  /* the console redraws */
    }
    lent_to = borrower;
    printf("screens: the display and the keyboard lent to pid %d\n", borrower);
    return 0;
}

static void lend_ends(const char *why) {
    int borrower = lent_to;
    lent_to = 0;
    unwatch(borrower);
    printf("screens: the display and the keyboard back, %s\n", why);
    reclaim();
}

/* The screens of programs that have ended, and a borrower that ended:
 * sig_on_end refuses a process that is gone (and watches again one that is
 * not). */
static void close_ended(void) {
    if (lent_to != 0 && sig_on_end(lent_to, end_bit) != 0) {
        lend_ends("their borrower ended");
    }
    for (int i = 0; i < screen_count;) {
        struct screen *screen = screens[i];
        if (sig_on_end(screen->owner, end_bit) != 0) {
            close_screen(screen, "its program ended");
        } else {
            i++;
        }
    }
}

/* --- Requests ------------------------------------------------------------------ */

/* Carry out one program's request; `sender` is its pid. */
static void serve(struct screen_request *request, int sender) {
    request->result = -1;
    if (request->op == SCREEN_OPEN) {
        struct screen *screen = open_screen(sender, request);
        request->screen = screen;
        request->id = screen != NULL ? screen->id : 0;
        request->pid = getpid();
        request->result = screen != NULL ? 0 : -1;
        return;
    }
    if (request->op == SCREEN_LEND) {
        request->result = lend(sender);
        return;
    }
    if (request->op == SCREEN_RETURN) {
        if (sender == lent_to) {
            lend_ends("returned");
            request->result = 0;
        }
        return;
    }
    struct screen *screen = request->screen;
    if (index_of(screen) < 0 || screen->owner != sender) {
        return;  /* not a screen of its own */
    }
    switch (request->op) {
    case SCREEN_DAMAGE:
        if (screen == front) {
            copy_rect(request->x, request->y, request->width, request->height);
        }
        request->result = 0;
        break;
    case SCREEN_FRONT:
        if (screen != front) {
            to_front(screen);
        }
        request->result = 0;
        break;
    case SCREEN_CLOSE:
        close_screen(screen, "closed by its program");
        request->result = 0;
        break;
    default:
        break;
    }
}

/* --- Keys ---------------------------------------------------------------------- */

static int is_meta_m(const struct input_event *event) {
    return event->keycode == KEY_M && (event->modifiers & INPUT_MOD_GUI) != 0;
}

/* Meta+M brings the next screen to front; every other key goes to the front
 * screen's program. Returns whether the event was the manager's own. */
static int hotkey(const struct input_event *event) {
    if (event->type == INPUT_EVENT_KEY_DOWN && is_meta_m(event)) {
        meta_m_held = 1;
        if (screen_count > 1) {
            int i = index_of(front);
            to_front(screens[(i + 1) % screen_count]);
        }
        return 1;
    }
    if (event->type == INPUT_EVENT_KEY_UP && event->keycode == KEY_M && meta_m_held) {
        meta_m_held = 0;
        return 1;
    }
    return 0;
}

/* Send the screen's program its waiting events, a message at a time: the
 * next once it has replied to the last. */
static void send_events(struct screen *screen) {
    if (screen->out != NULL || screen->head == screen->tail) {
        return;
    }
    struct batch *b = NULL;
    for (int i = 0; i < MAX_BATCHES && b == NULL; i++) {
        b = batches[i].busy ? NULL : &batches[i];
    }
    if (b == NULL) {
        return;  /* all out: when one comes back */
    }
    int n = 0;
    while (n < SCREEN_BATCH && screen->tail != screen->head) {
        b->events.events[n++] = screen->pending[screen->tail++ % SCREEN_PENDING];
    }
    b->events.count = n;
    b->screen = screen;
    b->busy = 1;
    if (port_put(screen->port, &b->events, reply_port) != 0) {
        b->busy = 0;  /* its program has ended: the screen closes at its end signal */
        return;
    }
    screen->out = b;
}

/* A message of events back from a program, replied or returned. */
static void batch_back(void *msg) {
    for (int i = 0; i < MAX_BATCHES; i++) {
        struct batch *b = &batches[i];
        if (b->busy && &b->events == msg) {
            struct screen *screen = b->screen;
            b->busy = 0;
            b->screen = NULL;
            if (screen != NULL) {
                screen->out = NULL;
                send_events(screen);
            }
            return;
        }
    }
}

/* Fill any free batches the screens waited for. */
static void send_all(void) {
    for (int i = 0; i < screen_count; i++) {
        send_events(screens[i]);
    }
}

static void deliver(struct screen *screen, const struct input_event *event) {
    if (screen->head - screen->tail == SCREEN_PENDING) {
        screen->lost++;
        return;
    }
    screen->pending[screen->head++ % SCREEN_PENDING] = *event;
}

static void route(const struct input_event *event) {
    if (!hotkey(event) && front != NULL) {
        deliver(front, event);
    }
}

/* --- The programs -------------------------------------------------------------- */

/* Start each "program [args]" of argv, separated by "+". */
static void start_programs(int argc, char **argv, int child_bit) {
    int start = 0;
    for (int i = 0; i <= argc; i++) {
        if (i < argc && strcmp(argv[i], "+") != 0) {
            continue;
        }
        if (i > start && program_count < MAX_PROGRAMS) {
            int pid = spawn_flags(argv[start], i - start, argv + start,
                                  SPAWN_NOWAIT | SPAWN_SIGNAL(child_bit));
            if (pid < 0) {
                printf("screens: cannot run %s\n", argv[start]);
            } else {
                programs[program_count++] = pid;
            }
        }
        start = i + 1;
    }
}

static void reap_programs(void) {
    for (int i = 0; i < program_count;) {
        int code;
        if (waitpid(programs[i], &code, WNOHANG) == programs[i]) {
            programs[i] = programs[--program_count];
        } else {
            i++;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: screens program [args] [+ program [args]]...\n", stderr);
        return 2;
    }
    if (fb_info(&display) != 0 || display.bpp != 32) {
        fputs("screens: no 32-bit display\n", stderr);
        return 1;
    }
    if (port_find(SCREENS_PORT) >= 0) {
        fputs("screens: a screen manager runs already\n", stderr);
        return 1;
    }
    int port = port_create(SCREENS_PORT);
    reply_port = port_create(NULL);
    int port_bit = sig_alloc(-1), child_bit = sig_alloc(-1);
    input_bit = sig_alloc(-1);
    end_bit = sig_alloc(-1);
    if (port < 0 || reply_port < 0 || port_bit < 0 || input_bit < 0 || child_bit < 0 ||
        end_bit < 0 || port_signal(port, port_bit) != 0 || port_signal(reply_port, port_bit) != 0) {
        fputs("screens: no port\n", stderr);
        return 1;
    }
    printf("screens: %ux%u, Meta+M brings the next screen to front\n", display.width,
           display.height);
    fflush(stdout);
    start_programs(argc - 1, argv + 1, child_bit);

    unsigned all = SIG_MASK(port_bit) | SIG_MASK(input_bit) | SIG_MASK(child_bit) |
                   SIG_MASK(end_bit);
    while (screen_count > 0 || program_count > 0 || lent_to != 0) {
        unsigned got = sig_wait(all);
        struct port_message message;
        while (port_get(reply_port, &message) == 1) {
            batch_back(message.msg);  /* replied, or returned by a program that ended */
        }
        while (port_get(port, &message) == 1) {
            if (message.kind == PORT_MESSAGE) {
                serve(message.msg, message.sender);
                port_reply(message.msg);
            }
        }
        struct input_event events[INPUT_READ_MAX];
        int count;
        while ((count = input_read(events, INPUT_READ_MAX)) > 0) {
            for (int i = 0; i < count; i++) {
                route(&events[i]);
            }
        }
        if (got & SIG_MASK(end_bit)) {
            close_ended();
        }
        if (got & SIG_MASK(child_bit)) {
            reap_programs();
        }
        send_all();
        reclaim();
        fflush(stdout);
    }
    printf("screens: done\n");
    port_delete(port);
    port_delete(reply_port);
    return 0;
}
