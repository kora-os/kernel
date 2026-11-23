/* libscreen: screens of the screen manager (user/screens.c), as AmigaOS
 * Intuition's OpenScreen().
 *
 * A screen is a pixel buffer as large as the display, 32 bits per pixel,
 * that the program opening it allocates and draws into; the manager only
 * reads it. After drawing, the program says which rectangle changed; when
 * its screen is in front, the manager copies that rectangle to the display.
 * Only one screen is in front: Meta+M (Command or the Windows key, with M)
 * brings the next one there, and the keys go to the program whose screen is
 * in front, as events (<libk/koraos.h>'s struct input_event).
 *
 * The manager draws the mouse pointer over the front screen, and its
 * program gets the mouse's events too (INPUT_EVENT_MOUSE): x and y are the
 * pointer's position on the screen, in pixels (INPUT_FLAG_ABSOLUTE is not
 * set), with the buttons held and the wheel's turn. Moves the program has not
 * taken yet are merged: it gets where the pointer is, not every step.
 *
 *   screen_t *s = screen_open("mydemo");   NULL: no screen manager runs
 *   ... draw into s->pixels (s->width x s->height, s->pitch bytes a row) ...
 *   screen_damage(s, 0, 0, s->width, s->height);
 *   for (;;) {
 *       sig_wait(screen_signal(s) | other signals);
 *       struct input_event event;
 *       while (screen_event(s, &event)) { ... }
 *       if (screen_gone(s)) { ... the manager ended: stop ... }
 *   }
 *   screen_close(s);
 *
 * Either side may end, crashed or not, and neither ever writes into the
 * other's memory, so neither is left writing into memory freed: there is no
 * memory protection to catch it. The pixels are the program's own. The
 * events come as messages from the manager on the screen's own port, in the
 * manager's memory, which the kernel keeps until they are replied. A
 * program's screens close when it ends; when the manager ends, its screens
 * are gone (screen_gone), and the screen's signal says so.
 */
#ifndef LIBSCREEN_SCREEN_H
#define LIBSCREEN_SCREEN_H

#include "libk/koraos.h"

/* The manager's public message port. */
#define SCREENS_PORT "screens"

/* Events one message carries. */
#define SCREEN_BATCH 32

#define SCREEN_TITLE_MAX 32

/* Key and mouse events for a screen's program: a message from the manager,
 * in its memory, on the screen's port. The program reads them and replies. */
struct screen_events {
    int count;
    struct input_event events[SCREEN_BATCH];
};

/* A program's request to the manager, a message on its port (in the
 * program's memory) that the manager replies to once it is done. */
#define SCREEN_OPEN 1    /* title, pixels, width, height, pitch, port in; screen, id, pid out */
#define SCREEN_DAMAGE 2  /* screen, x, y, width, height in */
#define SCREEN_FRONT 3   /* screen in */
#define SCREEN_CLOSE 4   /* screen in */
#define SCREEN_LEND 5    /* nothing in: the sender borrows the display and keys */
#define SCREEN_RETURN 6  /* nothing in: the sender returns them */

struct screen_request {
    int op;
    int result;          /* 0, or -1: set by the manager before its reply */
    void *screen;        /* the manager's name for the screen */
    int id;
    int pid;             /* SCREEN_OPEN: the manager's, to watch for its end */
    int x, y, width, height;
    unsigned pitch;      /* bytes a row */
    unsigned *pixels;    /* the program's */
    int port;            /* where the screen's events go */
    const char *title;
};

/* The program's side of a screen. */
typedef struct screen_handle {
    unsigned width, height;
    unsigned pitch;                /* bytes a row */
    unsigned *pixels;              /* this program's memory */
    int id;
    void *screen;                  /* the manager's name for it */
    int manager;                   /* its port, and its pid */
    int manager_pid;
    int reply_port;
    int event_port;
    int bit;                       /* the signal events and the manager's end set */
    struct screen_events *batch;   /* events taken and not all read: the manager's */
    int next;                      /* the next one of them */
    int gone;                      /* the manager has ended */
    struct screen_request request;
} screen_t;

/* Open a screen titled `title`, put it in front, and make the calling thread
 * the one its events signal. NULL when no screen manager runs, or it
 * refused, or there is no memory. */
screen_t *screen_open(const char *title);

/* The signal mask that the screen's events, and the manager's end, set. */
unsigned screen_signal(const screen_t *s);

/* Take the oldest event for the screen, a key's or the mouse's: 1, or 0
 * when there is none. */
int screen_event(screen_t *s, struct input_event *event);

/* Whether the manager has ended: the screen is gone, nothing more shows and
 * no key comes. Close it then (or carry on without it). */
int screen_gone(screen_t *s);

/* The rectangle drawn since the last call (clipped to the screen): the
 * manager copies it to the display if the screen is in front, and returns
 * when it has. */
void screen_damage(screen_t *s, int x, int y, int width, int height);

/* Bring the screen to front. */
void screen_to_front(screen_t *s);

/* Close the screen (if the manager still runs) and free it: its pixels and
 * the handle. */
void screen_close(screen_t *s);

/* Borrow the display and the keyboard from the screen manager, as a program
 * that drives them itself does (fb_claim, input_claim): the manager gives
 * them back to the kernel's console, and the program claims them from there.
 * Screens stay open out of sight, and no screen opens meanwhile. They go back
 * to the manager when the program calls screens_return(), after giving up
 * its own claims, or ends. 0, or -1: no screen manager runs, or it has lent
 * them already. */
int screens_borrow(void);
int screens_return(void);

#endif
