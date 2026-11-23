/* gfxdemo: draw straight into the framebuffer. With no argument it paints a
 * color gradient; given a color name (red/green/blue/white) it fills the screen
 * with that color. Demonstrates fb_info plus argv-driven behavior. Under the
 * screen manager (screens) it paints a screen of its own instead, and closes
 * it on Escape.
 */
#include "libk/koraos.h"
#include "libscreen/screen.h"

static unsigned int color_from_name(const char *name) {
    if (strcmp(name, "red") == 0) {
        return 0x00ff0000u;
    }
    if (strcmp(name, "green") == 0) {
        return 0x0000ff00u;
    }
    if (strcmp(name, "blue") == 0) {
        return 0x000000ffu;
    }
    if (strcmp(name, "white") == 0) {
        return 0x00ffffffu;
    }
    return 0;  // unknown -> gradient
}

/* Paint a width x height picture into rows of `pitch` bytes. */
static void paint(volatile unsigned int *pixels, unsigned int width, unsigned int height,
                  unsigned int pitch, unsigned int fill) {
    unsigned int stride = pitch / 4;
    for (unsigned int y = 0; y < height; y++) {
        for (unsigned int x = 0; x < width; x++) {
            unsigned int color;
            if (fill != 0) {
                color = fill;
            } else {
                // Gradient: red rises with x, green with y.
                unsigned int r = (x * 255) / width;
                unsigned int g = (y * 255) / height;
                color = (r << 16) | (g << 8);
            }
            pixels[y * stride + x] = color;
        }
    }
}

int main(int argc, char **argv) {
    unsigned int fill = (argc > 1) ? color_from_name(argv[1]) : 0;

    // Under the screen manager, on a screen of its own until Escape.
    screen_t *screen = screen_open("gfxdemo");
    if (screen != NULL) {
        paint(screen->pixels, screen->width, screen->height, screen->pitch, fill);
        screen_damage(screen, 0, 0, (int)screen->width, (int)screen->height);
        printf("gfxdemo: painted %ux%u on screen %d, Escape closes it\n", screen->width,
               screen->height, screen->id);
        fflush(stdout);
        for (;;) {
            sig_wait(screen_signal(screen));
            struct input_event event;
            while (screen_event(screen, &event)) {
                if (event.type == INPUT_EVENT_KEY_DOWN && event.keycode == KEY_ESCAPE) {
                    screen_close(screen);
                    return 0;
                }
            }
            if (screen_gone(screen)) {  /* the manager ended */
                screen_close(screen);
                return 1;
            }
        }
    }

    struct fb_info fb;
    if (fb_info(&fb) != 0) {
        fputs("gfxdemo: no framebuffer\n", stderr);
        return 1;
    }
    paint((volatile unsigned int *)fb.addr, fb.width, fb.height, fb.pitch, fill);
    printf("gfxdemo: painted %ux%u\n", fb.width, fb.height);
    return 0;
}
