/* libscreen: screens of the screen manager (user/screens.c), as AmigaOS
 * Intuition's OpenScreen().
 *
 * A screen is a pixel buffer as large as the display, 32 bits per pixel,
 * that the manager allocates and the program that opened it draws into.
 * After drawing, the program says which rectangle changed; when its screen
 * is in front, the manager copies that rectangle to the display. Only one
 * screen is in front: Meta+M (Command or the Windows key, with M) brings the
 * next one there, and the keys go to the program whose screen is in front,
 * as events (<libk/koraos.h>'s struct input_event).
 *
 *   screen_t *s = screen_open("mydemo");   NULL: no screen manager runs
 *   ... draw into s->pixels (s->width x s->height, s->pitch bytes a row) ...
 *   screen_damage(s, 0, 0, s->width, s->height);
 *   for (;;) {
 *       sig_wait(screen_signal(s) | other signals);
 *       struct input_event event;
 *       while (screen_event(s, &event)) { ... }
 *   }
 *   screen_close(s);
 *
 * A program's screens close when it ends. There is no memory protection: the
 * pixels and the events are the manager's memory, valid until the screen
 * closes or the manager ends.
 */
#ifndef LIBSCREEN_SCREEN_H
#define LIBSCREEN_SCREEN_H

#include "libk/koraos.h"

/* The manager's public message port. */
#define SCREENS_PORT "screens"

/* Events a screen holds for its program; more are dropped and counted. A
 * line typed at once (the debug console's `type`) is two events a key, four
 * with Shift. */
#define SCREEN_EVENTS 256  /* a power of two */

#define SCREEN_TITLE_MAX 32

/* A screen, in the manager's memory and shared with the program that opened
 * it. The event ring has one writer (the manager: events[], head, lost) and
 * one reader (the program: tail). */
struct screen {
    int id;                 /* 1, 2, ... in the order they were opened */
    int owner;              /* the pid of the program that opened it */
    int owner_tid;          /* the thread its events signal, and the bit */
    int owner_bit;
    unsigned width, height;
    unsigned pitch;         /* bytes a row */
    unsigned *pixels;
    char title[SCREEN_TITLE_MAX];
    unsigned head, tail;    /* events written and read, counting up */
    unsigned lost;          /* events dropped on a full ring */
    struct input_event events[SCREEN_EVENTS];
};

/* A program's request to the manager, a message on its port (in the
 * program's memory) that the manager replies to once it is done. */
#define SCREEN_OPEN 1    /* title, tid, bit in; screen out */
#define SCREEN_DAMAGE 2  /* screen, x, y, width, height in */
#define SCREEN_FRONT 3   /* screen in */
#define SCREEN_CLOSE 4   /* screen in */
#define SCREEN_LEND 5    /* nothing in: the sender borrows the display and keys */
#define SCREEN_RETURN 6  /* nothing in: the sender returns them */

struct screen_request {
    int op;
    int result;          /* 0, or -1: set by the manager before its reply */
    struct screen *screen;
    int x, y, width, height;
    int tid, bit;
    const char *title;
};

/* The program's side of a screen. */
typedef struct screen_handle {
    unsigned width, height;
    unsigned pitch;
    unsigned *pixels;
    int id;
    struct screen *screen;  /* the manager's */
    int manager;            /* its port */
    int reply_port;
    int bit;                /* the signal events set */
    struct screen_request request;
} screen_t;

/* Open a screen titled `title`, put it in front, and make the calling thread
 * the one its events signal. NULL when no screen manager runs, or it
 * refused (no memory). */
screen_t *screen_open(const char *title);

/* The signal mask that the screen's events set, for sig_wait. */
unsigned screen_signal(const screen_t *s);

/* Take the oldest key event for the screen: 1, or 0 when there is none. */
int screen_event(screen_t *s, struct input_event *event);

/* The rectangle drawn since the last call (clipped to the screen): the
 * manager copies it to the display if the screen is in front, and returns
 * when it has. */
void screen_damage(screen_t *s, int x, int y, int width, int height);

/* Bring the screen to front. */
void screen_to_front(screen_t *s);

/* Close the screen and free the handle. */
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
