#include "sys/syscall.h"
#include "arch/trapframe.h"
#include "common.h"
#include "fs/fat32.h"
#include "lib/string.h"
#include "mini_uart.h"
#include "proc/task.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "video/console_fb.h"

// lseek whence values; must match user/libk/koraos.h.
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Kernel-side mirrors of the userland struct dirent / struct stat
// (user/libk/koraos.h). Layouts must match exactly: the syscalls fill these
// through user pointers.
struct kdirent {
    uint64_t size;
    uint32_t is_dir;
    char name[FAT32_NAME_MAX + 1];
};

struct kstat {
    uint64_t size;
    uint32_t is_dir;
};

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

// Look up an open file/directory handle by fd (>= FD_BASE) in the current task,
// or NULL if the fd is out of range or not open.
static open_file_t *fd_lookup(int fd) {
    task_t *t = task_current();
    if (t == NULL || fd < FD_BASE || fd >= FD_BASE + MAX_OPEN_FILES) {
        return NULL;
    }
    open_file_t *of = &t->files[fd - FD_BASE];
    return of->used ? of : NULL;
}

// Read a line from the UART, echoing as it goes and honouring backspace.
// Returns at a newline or when the buffer fills.
static long read_console_line(char *buf, uint64_t len) {
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

// read(fd, buf, len): fd 0 (stdin) reads a console line; fd >= FD_BASE reads
// from an open file. Returns bytes read, or -1.
static long sys_read(int fd, char *buf, uint64_t len) {
    if (len == 0) {
        return 0;
    }
    if (!uptr_ok((uint64_t)buf, len)) {
        return -1;
    }
    if (fd == 0) {
        return read_console_line(buf, len);
    }
    open_file_t *of = fd_lookup(fd);
    if (of == NULL || of->file.is_dir) {
        return -1;
    }
    long n = fat32_read(&of->file, buf, (uint32_t)len);
    return n < 0 ? -1 : n;
}

// open(path, flags): resolve an absolute path to a file or directory and give
// it an fd in the calling task's table. Only O_RDONLY is supported.
static long sys_open(const char *path, int flags) {
    (void)flags;  // read-only filesystem
    if (!uptr_ok((uint64_t)path, 1)) {
        return -1;
    }
    task_t *t = task_current();
    if (t == NULL) {
        return -1;
    }
    int idx = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!t->files[i].used) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;  // fd table full
    }
    fat32_file_t f;
    int rc = fat32_open(path, &f);
    if (rc == FS_ERR_ISDIR) {
        rc = fat32_opendir(path, &f);  // a directory: open it for readdir
    }
    if (rc != 0) {
        return -1;
    }
    t->files[idx].file = f;
    t->files[idx].used = true;
    return FD_BASE + idx;
}

static long sys_close(int fd) {
    open_file_t *of = fd_lookup(fd);
    if (of == NULL) {
        return -1;
    }
    of->used = false;
    return 0;
}

// lseek(fd, offset, whence): reposition a file's cursor within [0, size].
static long sys_lseek(int fd, long offset, int whence) {
    open_file_t *of = fd_lookup(fd);
    if (of == NULL || of->file.is_dir) {
        return -1;
    }
    long base;
    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = (long)of->file.pos;
        break;
    case SEEK_END:
        base = (long)of->file.size;
        break;
    default:
        return -1;
    }
    long np = base + offset;
    if (np < 0 || (uint64_t)np > of->file.size) {
        return -1;
    }
    of->file.pos = (uint32_t)np;
    return np;
}

// readdir(fd, out): fetch the next entry from an open directory. Returns 1 for
// an entry, 0 at end of directory, or -1 on error.
static long sys_readdir(int fd, struct kdirent *out) {
    open_file_t *of = fd_lookup(fd);
    if (of == NULL || !of->file.is_dir) {
        return -1;
    }
    if (!uptr_ok((uint64_t)out, sizeof(*out))) {
        return -1;
    }
    fat32_dirent_t de;
    int r = fat32_readdir(&of->file, &de);
    if (r != 1) {
        return r < 0 ? -1 : 0;
    }
    out->size = de.size;
    out->is_dir = de.is_dir;
    memcpy(out->name, de.name, sizeof(out->name));
    return 1;
}

// stat(path, out): report the size and type of an absolute path.
static long sys_stat(const char *path, struct kstat *out) {
    if (!uptr_ok((uint64_t)path, 1) || !uptr_ok((uint64_t)out, sizeof(*out))) {
        return -1;
    }
    fat32_stat_t st;
    if (fat32_stat(path, &st) != 0) {
        return -1;
    }
    out->size = st.size;
    out->is_dir = st.is_dir;
    return 0;
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
    case SYS_open:
        ret = sys_open((const char *)a0, (int)a1);
        break;
    case SYS_close:
        ret = sys_close((int)a0);
        break;
    case SYS_lseek:
        ret = sys_lseek((int)a0, (long)a1, (int)a2);
        break;
    case SYS_readdir:
        ret = sys_readdir((int)a0, (struct kdirent *)a1);
        break;
    case SYS_stat:
        ret = sys_stat((const char *)a0, (struct kstat *)a1);
        break;
    default:
        ret = -1;
        break;
    }

    tf->regs[0] = (uint64_t)ret;
}
