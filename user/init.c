/* KoraOS init: the first user program the kernel starts. It exercises the
 * Step 5 syscalls -- framebuffer access (fb_info), heap allocation (sbrk),
 * process spawning (spawn), and keyboard input (read) -- so a single run shows
 * the whole userland surface working together.
 */
#include "libk/koraos.h"

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void puts_(const char *s) {
    write(1, s, str_len(s));
}

static void put_uint(unsigned long v) {
    char tmp[24];
    int i = 0;
    if (v == 0) {
        puts_("0");
        return;
    }
    while (v > 0) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    char out[24];
    int j = 0;
    while (i > 0) {
        out[j++] = tmp[--i];
    }
    write(1, out, (size_t)j);
}

// Fill a rectangle directly in the framebuffer (32bpp, flat identity map).
static void draw_rect(const struct fb_info *fb, unsigned x0, unsigned y0,
                      unsigned w, unsigned h, unsigned int color) {
    volatile unsigned int *pixels = (volatile unsigned int *)fb->addr;
    unsigned stride = fb->pitch / 4;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            pixels[(y0 + y) * stride + (x0 + x)] = color;
        }
    }
}

int main(void) {
    puts_("init: KoraOS userland up (pid ");
    put_uint((unsigned long)getpid());
    puts_(")\n");

    // Framebuffer: report geometry and paint a green square.
    struct fb_info fb;
    if (fb_info(&fb) == 0) {
        puts_("init: framebuffer ");
        put_uint(fb.width);
        puts_("x");
        put_uint(fb.height);
        puts_(", drawing a square\n");
        draw_rect(&fb, 64, 64, 160, 160, 0x0000ff00u);
    } else {
        puts_("init: no framebuffer\n");
    }

    // Heap: grow the break and use the memory.
    char *buf = (char *)sbrk(32);
    if (buf != (char *)-1) {
        const char *msg = "heap works\n";
        size_t n = str_len(msg);
        for (size_t i = 0; i < n; i++) {
            buf[i] = msg[i];
        }
        puts_("init: sbrk gave a heap page; it says: ");
        write(1, buf, n);
    } else {
        puts_("init: sbrk failed\n");
    }

    // Process model: spawn a child.
    spawn("hello", 0, 0);

    // Keyboard: echo one typed line.
    puts_("init: type a line and press enter: ");
    char line[64];
    long n = read(0, line, sizeof(line));
    if (n > 0) {
        puts_("init: you typed: ");
        write(1, line, (size_t)n);
    }

    puts_("init: done\n");
    return 0;
}
