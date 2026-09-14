#include "sys/syscall.h"
#include "arch/trapframe.h"
#include "common.h"
#include "mini_uart.h"
#include "proc/task.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "video/console_fb.h"

// Kernel-side mirror of the userland struct fb_info (user/libk/koraos.h). The
// layout must match exactly: the syscall fills this through a user pointer.
struct fb_info {
    uint64_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

// Rough sanity check for a user-supplied pointer + length. The flat identity map
// gives no real isolation, so this only rejects null/wild pointers and ranges
// that overflow or fall outside the low-4GB identity map -- enough to turn a bad
// user pointer into an error return instead of a kernel fault.
static bool uptr_ok(uint64_t addr, uint64_t len) {
    if (addr == 0) {
        return false;
    }
    uint64_t end = addr + len;
    if (end < addr) {
        return false;  // overflow
    }
    if (end > 0x100000000ULL) {
        return false;  // beyond the identity map
    }
    return true;
}

// Emit one character to every attached console: the UART (translating LF to
// CR+LF) and the framebuffer screen.
static void con_putc(char c) {
    if (c == '\n') {
        uart_putc('\r');
    }
    uart_putc((unsigned char)c);
    screen_putc(c);
}

// write(fd, buf, len): fd 1 (stdout) and 2 (stderr) go to the console.
static long sys_write(int fd, const char *buf, uint64_t len) {
    if (fd != 1 && fd != 2) {
        return -1;
    }
    if (!uptr_ok((uint64_t)buf, len)) {
        return -1;
    }
    for (uint64_t i = 0; i < len; i++) {
        con_putc(buf[i]);
    }
    return (long)len;
}

// read(fd, buf, len): fd 0 (stdin) reads a line from the UART, echoing as it
// goes and honouring backspace. Returns at a newline or when the buffer fills.
static long sys_read(int fd, char *buf, uint64_t len) {
    if (fd != 0) {
        return -1;
    }
    if (len == 0) {
        return 0;
    }
    if (!uptr_ok((uint64_t)buf, len)) {
        return -1;
    }
    uint64_t i = 0;
    while (i < len) {
        unsigned char c = uart_getc();
        if (c == '\r') {
            c = '\n';
        }
        if (c == 0x7f || c == 0x08) {  // delete / backspace
            if (i > 0) {
                i--;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
            continue;
        }
        con_putc((char)c);  // echo
        buf[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    return (long)i;
}

// sbrk(increment): grow (or shrink) the calling task's heap, which is allocated
// lazily on first use. Returns the previous break, or -1 on failure.
static long sys_sbrk(long increment) {
    task_t *t = task_current();
    if (t == NULL) {
        return -1;
    }
    if (t->heap_base == 0) {
        void *heap = frame_alloc_pages(USER_HEAP_PAGES);
        if (heap == NULL) {
            return -1;
        }
        t->heap = heap;
        t->heap_pages = USER_HEAP_PAGES;
        t->heap_base = (uint64_t)heap;
        t->heap_brk = t->heap_base;
        t->heap_end = t->heap_base + (uint64_t)USER_HEAP_PAGES * PAGE_SIZE;
    }
    uint64_t old = t->heap_brk;
    uint64_t nb = old + (uint64_t)increment;
    if (nb < t->heap_base || nb > t->heap_end) {
        return -1;
    }
    t->heap_brk = nb;
    return (long)old;
}

// spawn(name, argc, argv): load and run an embedded program with arguments.
static long sys_spawn(const char *name, int argc, char *const argv[]) {
    if (!uptr_ok((uint64_t)name, 1)) {
        return -1;
    }
    if (argc < 0) {
        return -1;
    }
    if (argc > 0 && !uptr_ok((uint64_t)argv, (uint64_t)argc * sizeof(char *))) {
        return -1;
    }
    return task_spawn(name, argc, argv);
}

// fb_info(out): report the active screen's framebuffer geometry and address so
// a user program can draw straight into it (flat identity map).
static long sys_fb_info(struct fb_info *out) {
    if (!uptr_ok((uint64_t)out, sizeof(*out))) {
        return -1;
    }
    const framebuffer_info_t *fb = screen_framebuffer();
    if (fb == NULL || fb->buffer == NULL) {
        return -1;
    }
    out->addr = (uint64_t)fb->buffer;
    out->width = fb->width;
    out->height = fb->height;
    out->pitch = fb->pitch;
    out->bpp = fb->depth;
    return 0;
}

void syscall_handle(struct trapframe *tf) {
    uint64_t num = tf->regs[8];
    uint64_t a0 = tf->regs[0];
    uint64_t a1 = tf->regs[1];
    uint64_t a2 = tf->regs[2];
    long ret = -1;

    switch (num) {
    case SYS_write:
        ret = sys_write((int)a0, (const char *)a1, a2);
        break;
    case SYS_exit:
        task_exit((int)a0);  // does not return
        break;
    case SYS_read:
        ret = sys_read((int)a0, (char *)a1, a2);
        break;
    case SYS_sbrk:
        ret = sys_sbrk((long)a0);
        break;
    case SYS_spawn:
        ret = sys_spawn((const char *)a0, (int)a1, (char *const *)a2);
        break;
    case SYS_wait:
        ret = task_wait((int)a0);
        break;
    case SYS_getpid:
        ret = task_getpid();
        break;
    case SYS_yield:
        ret = 0;  // cooperative single-run: nothing to yield to yet
        break;
    case SYS_fb_info:
        ret = sys_fb_info((struct fb_info *)a0);
        break;
    default:
        ret = -1;
        break;
    }

    tf->regs[0] = (uint64_t)ret;
}
