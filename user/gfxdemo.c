/* gfxdemo: draw straight into the framebuffer. With no argument it paints a
 * color gradient; given a color name (red/green/blue/white) it fills the screen
 * with that color. Demonstrates fb_info plus argv-driven behavior.
 */
#include "libk/koraos.h"

static unsigned int color_from_name(const char *name) {
    if (kstreq(name, "red")) {
        return 0x00ff0000u;
    }
    if (kstreq(name, "green")) {
        return 0x0000ff00u;
    }
    if (kstreq(name, "blue")) {
        return 0x000000ffu;
    }
    if (kstreq(name, "white")) {
        return 0x00ffffffu;
    }
    return 0;  // unknown -> gradient
}

int main(int argc, char **argv) {
    struct fb_info fb;
    if (fb_info(&fb) != 0) {
        kputs("gfxdemo: no framebuffer\n");
        return 1;
    }

    volatile unsigned int *pixels = (volatile unsigned int *)fb.addr;
    unsigned int stride = fb.pitch / 4;
    unsigned int fill = (argc > 1) ? color_from_name(argv[1]) : 0;

    for (unsigned int y = 0; y < fb.height; y++) {
        for (unsigned int x = 0; x < fb.width; x++) {
            unsigned int color;
            if (fill != 0) {
                color = fill;
            } else {
                // Gradient: red rises with x, green with y.
                unsigned int r = (x * 255) / fb.width;
                unsigned int g = (y * 255) / fb.height;
                color = (r << 16) | (g << 8);
            }
            pixels[y * stride + x] = color;
        }
    }

    kputs("gfxdemo: painted ");
    kput_int((long)fb.width);
    kputs("x");
    kput_int((long)fb.height);
    kputs("\n");
    return 0;
}
