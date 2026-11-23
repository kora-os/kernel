// SPDX-License-Identifier: GPL-3.0-or-later
// EL0 fixture: a client of the screen manager (user/screens.c, libscreen).
//   screenprobe COLOUR   opens a screen titled "screenprobe COLOUR" (red,
//                        green, blue or white), fills it, and prints
//                        "screenprobe: COLOUR on screen N, pid P". Then it
//                        prints each key the screen gets ("screenprobe
//                        COLOUR: key down 0x04 mods 0x0000 char 0x61") until
//                        Escape, closes the screen and prints "screenprobe:
//                        COLOUR closed"; or until x, and ends without closing
//                        it ("screenprobe: COLOUR ends, screen open"), as a
//                        program that crashes. Exit 1 when no screen manager
//                        runs.
#include "../../user/libk/koraos.h"
#include "../../user/libscreen/screen.h"

static unsigned colour_of(const char *name) {
    if (!strcmp(name, "red")) return 0x00FF0000u;
    if (!strcmp(name, "green")) return 0x0000FF00u;
    if (!strcmp(name, "blue")) return 0x000000FFu;
    return 0x00FFFFFFu;
}

int main(int argc, char **argv) {
    const char *name = argc > 1 ? argv[1] : "white";
    char title[SCREEN_TITLE_MAX];
    snprintf(title, sizeof(title), "screenprobe %s", name);
    screen_t *s = screen_open(title);
    if (s == NULL) {
        printf("screenprobe: no screen manager\n");
        return 1;
    }
    unsigned colour = colour_of(name);
    for (unsigned y = 0; y < s->height; y++) {
        unsigned *row = (unsigned *)((char *)s->pixels + (unsigned long)y * s->pitch);
        for (unsigned x = 0; x < s->width; x++) {
            row[x] = colour;
        }
    }
    screen_damage(s, 0, 0, (int)s->width, (int)s->height);
    printf("screenprobe: %s on screen %d, pid %d\n", name, s->id, getpid());
    fflush(stdout);
    for (;;) {
        sig_wait(screen_signal(s));
        struct input_event event;
        while (screen_event(s, &event)) {
            printf("screenprobe %s: key %s 0x%02x mods 0x%04x char 0x%x\n", name,
                   event.type == INPUT_EVENT_KEY_DOWN ? "down" : "up", event.keycode,
                   event.modifiers, event.codepoint);
            if (event.type == INPUT_EVENT_KEY_DOWN && event.codepoint == 'x') {
                printf("screenprobe: %s ends, screen open\n", name);
                return 0;  // the manager closes it when it sees the end
            }
            if (event.type == INPUT_EVENT_KEY_DOWN && event.keycode == KEY_ESCAPE) {
                screen_close(s);
                printf("screenprobe: %s closed\n", name);
                return 0;
            }
        }
        fflush(stdout);
    }
}
