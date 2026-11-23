/* jobdemo: the libk job API, TempleOS style. Renders the Mandelbrot set
 * (in integer fixed point) once as a single job
 * on core 0, then split into one job per online core, and reports both times
 * and the speedup. Job j renders every n-th row from row j, so the cheap and
 * the expensive rows share out evenly. Under the screen manager it draws on a
 * screen of its own, which stays until Enter or Escape; without one it only
 * computes. Either way each job returns the sum of its iteration counts, and
 * the two renderings must agree.
 *
 *   jobdemo [iterations]   iterations per pixel, 16 to 4096 (default 1024)
 */
#include "libk/koraos.h"
#include "libscreen/screen.h"

#define FRAC 28                   // fixed point: value * 2^28
#define FIX(x) ((long)((x) * (1L << FRAC)))
#define NOFB_WIDTH 640            // the picture's size without a screen
#define NOFB_HEIGHT 480

struct picture {
    unsigned width;
    unsigned height;
    unsigned max_iter;
    long step;                    // the width of a pixel, fixed point
    long left;                    // the real part at x = 0
    long top;                     // the imaginary part at y = 0
    volatile unsigned *pixels;    // NULL: compute only
    unsigned stride;              // pixels per row
};

struct band {
    const struct picture *pic;
    unsigned first;               // the first row
    unsigned every;               // then every n-th row
};

static unsigned color(unsigned i, unsigned max_iter) {
    if (i >= max_iter) {
        return 0;  // inside the set
    }
    unsigned r = (i * 9) & 255;
    unsigned g = (i * 4) & 255;
    unsigned b = (96 + i * 13) & 255;
    return (r << 16) | (g << 8) | b;
}

// Iterations until z = z^2 + c escapes |z| > 2, at most max_iter.
static unsigned escape(long cx, long cy, unsigned max_iter) {
    long zx = 0;
    long zy = 0;
    unsigned i = 0;
    for (; i < max_iter; i++) {
        long zx2 = (zx * zx) >> FRAC;
        long zy2 = (zy * zy) >> FRAC;
        if (zx2 + zy2 > FIX(4)) {
            break;
        }
        zy = ((zx * zy) >> (FRAC - 1)) + cy;
        zx = zx2 - zy2 + cx;
    }
    return i;
}

// One job: render its rows and return the iterations it spent.
static long render(void *arg) {
    const struct band *band = arg;
    const struct picture *pic = band->pic;
    long total = 0;
    for (unsigned y = band->first; y < pic->height; y += band->every) {
        long cy = pic->top + (long)y * pic->step;
        for (unsigned x = 0; x < pic->width; x++) {
            unsigned i = escape(pic->left + (long)x * pic->step, cy, pic->max_iter);
            total += i;
            if (pic->pixels != NULL) {
                pic->pixels[y * pic->stride + x] = color(i, pic->max_iter);
            }
        }
    }
    return total;
}

// Render the picture as `jobs` jobs on cores 0, 1, ...; returns the total
// iterations, or -1 if a job could not be queued. *ms gets the time taken.
static long run(const struct picture *pic, unsigned jobs, unsigned cpus, unsigned long *ms) {
    static struct band bands[16];
    job_t *queued[16];
    unsigned long start = uptime_us();
    for (unsigned j = 0; j < jobs; j++) {
        bands[j] = (struct band){pic, j, jobs};
        queued[j] = job_queue(render, &bands[j], (int)(j % cpus));
        if (queued[j] == NULL) {
            for (unsigned k = 0; k < j; k++) {
                job_result(queued[k]);
            }
            return -1;
        }
    }
    long total = 0;
    for (unsigned j = 0; j < jobs; j++) {
        total += job_result(queued[j]);
    }
    *ms = (uptime_us() - start + 500) / 1000;
    return total;
}

int main(int argc, char **argv) {
    unsigned max_iter = 1024;
    if (argc > 1) {
        max_iter = 0;
        for (const char *p = argv[1]; *p; p++) {
            max_iter = *p >= '0' && *p <= '9' && max_iter < 100000 ? max_iter * 10 + (unsigned)(*p - '0')
                                                                   : 100000;
        }
    }
    if (argc > 2 || max_iter < 16 || max_iter > 4096) {
        fputs("usage: jobdemo [iterations]   (16 to 4096, default 1024)\n", stderr);
        return 2;
    }
    struct sysinfo info;
    unsigned cpus = sysinfo(&info) == 0 && info.cpus > 0 ? info.cpus : 1;

    struct picture pic = {.width = NOFB_WIDTH, .height = NOFB_HEIGHT, .max_iter = max_iter};
    screen_t *screen = screen_open("jobdemo");
    if (screen != NULL) {
        pic.width = screen->width;
        pic.height = screen->height;
        pic.pixels = screen->pixels;
        pic.stride = screen->pitch / 4;
    }
    // Real parts -2.5 to 1.0 across the width; centred vertically.
    pic.step = FIX(3.5) / (long)pic.width;
    pic.left = FIX(-2.5);
    pic.top = -(long)pic.height / 2 * pic.step;

    unsigned long one_ms = 0;
    unsigned long all_ms = 0;
    long one = run(&pic, 1, cpus, &one_ms);
    long all = run(&pic, cpus, cpus, &all_ms);
    if (one < 0 || all < 0 || one != all) {
        fputs(one < 0 || all < 0 ? "jobdemo: FAILED to queue the jobs\n"
                                 : "jobdemo: FAILED, the renderings differ\n",
              stderr);
        return 1;
    }
    unsigned long speedup = all_ms != 0 ? one_ms * 100 / all_ms : 100;
    if (screen != NULL) {
        screen_damage(screen, 0, 0, (int)screen->width, (int)screen->height);
    }
    // One line, so one write() once stdout sees the newline: no other
    // program's output can land in the middle of it.
    printf("jobdemo: %ux%u, %u iterations: 1 core %lu ms, %u %s %lu ms, speedup %lu.%02lux%s\n",
           pic.width, pic.height, max_iter, one_ms, cpus, cpus == 1 ? "core" : "cores", all_ms,
           speedup / 100, speedup % 100, screen != NULL ? ", press Enter" : "");
    fflush(stdout);
    // The picture stays on its screen until Enter or Escape there.
    while (screen != NULL) {
        sig_wait(screen_signal(screen));
        struct input_event event;
        while (screen != NULL && screen_event(screen, &event)) {
            if (event.type == INPUT_EVENT_KEY_DOWN &&
                (event.keycode == KEY_ENTER || event.keycode == KEY_ESCAPE)) {
                screen_close(screen);
                screen = NULL;
            }
        }
    }
    return 0;
}
