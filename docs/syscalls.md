# System Calls

KoraOS has a small, KoraOS-private syscall ABI. The calls are *shaped* like
their POSIX namesakes to ease a future libc shim, but the numbering is our own.

## Calling convention

`x8` holds the syscall number; arguments go in `x0`, `x1`, `x2` (AAPCS order);
`svc #0` traps into the kernel; the return value comes back in `x0`.

The numbers are defined in two places that **must stay in sync**:
[`include/sys/syscall.h`](../include/sys/syscall.h) (kernel) and
[`user/libk/abi.h`](../user/libk/abi.h) (userland). Userland C prototypes and
the `struct` layouts are in [`user/libk/koraos.h`](../user/libk/koraos.h); the
assembly stubs are in [`user/libk/syscall.S`](../user/libk/syscall.S). The
kernel dispatches them in [`src/sys/syscall.c`](../src/sys/syscall.c).

## Summary

| # | Name | Prototype | Returns |
|---|------|-----------|---------|
| 0 | `write` | `ssize_t write(int fd, const void *buf, size_t len)` | bytes written, or `-1` |
| 1 | `exit` | `void exit(int status)` | does not return |
| 2 | `read` | `ssize_t read(int fd, void *buf, size_t len)` | bytes read, `0` at EOF, or `-1` |
| 3 | `sbrk` | `void *sbrk(long increment)` | previous break, or `(void*)-1` |
| 4 | `spawn` | `int spawn(const char *name, int argc, char *const argv[])` | child pid, or `-1` |
| 5 | `wait` | `int wait(int pid)` | child exit code, or `-1` |
| 6 | `getpid` | `int getpid(void)` | current pid |
| 7 | `yield` | `void yield(void)` | `0` (currently a no-op) |
| 8 | `fb_info` | `int fb_info(struct fb_info *out)` | `0`, or `-1` |
| 9 | `open` | `int open(const char *path, int flags)` | fd (≥ 3), or `-1` |
| 10 | `close` | `int close(int fd)` | `0`, or `-1` |
| 11 | `lseek` | `long lseek(int fd, long offset, int whence)` | new offset, or `-1` |
| 12 | `readdir` | `int readdir(int fd, struct dirent *out)` | `1` entry, `0` end, `-1` error |
| 13 | `stat` | `int stat(const char *path, struct stat *out)` | `0`, or `-1` |

## Notes per call

- **`write`**: only `fd` 1 (stdout) and 2 (stderr) are valid; both go to the
  console (UART + framebuffer).
- **`read`**: `fd` 0 reads a line from the console (echoed, backspace honoured,
  returns at newline or when the buffer fills). `fd ≥ 3` reads from an open file.
- **`sbrk`**: grows (or shrinks) the caller's heap, allocated lazily on first
  use; returns the previous break so `sbrk(0)` reads the current break.
- **`spawn`**: the process model is **cooperative and nesting**: `spawn` loads
  the program and runs it to completion in EL0 while the caller is suspended,
  then returns the (now-exited) child's pid. Call `wait(pid)` afterwards to reap
  it and collect its exit code. `name` is resolved to a filesystem path: a bare
  name is looked up under `/bin`, an absolute path is used as-is (see
  [filesystem.md](filesystem.md)). `argv` entries are copied onto the child's
  stack and delivered as `main(argc, argv)`.
- **`open`**: `flags` must be `O_RDONLY` (the filesystem is read-only). Works on
  both files and directories; the resulting fd is used with `read` (files) or
  `readdir` (directories). fds 0/1/2 are the console; real files start at 3.
- **`lseek`**: `whence` is `SEEK_SET` / `SEEK_CUR` / `SEEK_END`; the new offset
  must land within `[0, size]`. Not valid on directory fds.
- **`readdir`**: returns one entry per call from a directory fd. Skips deleted
  entries, the volume label, and `.` / `..`. `name` is UTF-8.
- **`stat`**: reports size and whether the path is a directory.

## Structs and constants

From [`user/libk/koraos.h`](../user/libk/koraos.h) (mirrored kernel-side in
`src/sys/syscall.c`, keep the layouts identical):

```c
#define O_RDONLY 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define DIRENT_NAME_MAX 766   /* UTF-8 bytes incl. NUL (255 UTF-16 units * 3) */

struct dirent {
    unsigned long size;
    int           is_dir;
    char          name[DIRENT_NAME_MAX];
};

struct stat {
    unsigned long size;
    int           is_dir;
};

struct fb_info {           /* filled by fb_info() */
    unsigned long addr;    /* framebuffer base (physical == virtual) */
    unsigned int  width;
    unsigned int  height;
    unsigned int  pitch;   /* bytes per row */
    unsigned int  bpp;     /* bits per pixel */
};
```

## Adding a syscall

1. Add the number to **both** `include/sys/syscall.h` and `user/libk/abi.h`.
2. Add an assembly stub in `user/libk/syscall.S` (`SYSCALL name, SYS_NAME`).
3. Add the C prototype (and any structs) to `user/libk/koraos.h`.
4. Implement `sys_<name>` and add a `case` in `syscall_handle` in
   `src/sys/syscall.c`. Validate user pointers with `uptr_ok`.
