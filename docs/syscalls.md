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
| 3 | (retired) | was `sbrk` | `-1` |
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
| 14 | `alloc_pages` | `void *alloc_pages(size_t count)` | base of `count` zeroed pages, or `NULL` |
| 15 | `free_pages` | `int free_pages(void *base)` | `0`, or `-1` |
| 32 | `chdir` | `int chdir(const char *path)` | `0`, or `-1` |
| 33 | `getcwd` | `int getcwd(char *buf, size_t size)` | `0`, or `-1` |
| 34 | `volume_info` | `int volume_info(unsigned int index, struct volume_info *out)` | `1` item, `0` end, `-1` error |
| 35 | `assign` | `int assign(const char *name, const char *target)` | `0`, or `-1` |
| 36 | `assign_info` | `int assign_info(unsigned int index, struct assign_info *out)` | `1` item, `0` end, `-1` error |

## Notes per call

- **`write`**: only `fd` 1 (stdout) and 2 (stderr) are valid; both go to the
  console (UART + framebuffer).
- **`read`**: `fd` 0 reads a line from the console (echoed, backspace honoured,
  returns at newline or when the buffer fills). `fd ≥ 3` reads from an open file.
- **`alloc_pages`** / **`free_pages`**: Amiga-style memory: a run of `count`
  contiguous, zeroed 4 KB pages (`KORAOS_PAGE_SIZE`) anywhere in RAM, returned
  by its base address. The kernel records each run against the calling task, so
  `free_pages` needs no size, rejects addresses the task does not own, and
  everything still allocated is reclaimed when the task is reaped. libk's
  `malloc` builds on these (see
  [writing-userland-programs.md](writing-userland-programs.md)). There is no
  `sbrk`: with one flat address space, memory after a heap is usually someone
  else's, so a contiguous break cannot grow reliably.
- **`spawn`**: the process model is **cooperative and nesting**: `spawn` loads
  the program and runs it to completion in EL0 while the caller is suspended,
  then returns the (now-exited) child's pid. Call `wait(pid)` afterwards to reap
  it and collect its exit code. `name` is resolved to a filesystem path: a bare
  name is looked up through `c:` (initially `sys:bin`), with no fallback to
  `/bin` if the assign or command is missing; other names use the caller's
  volume-aware path resolver (see
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
- **`chdir`**: changes only the calling task's current directory. Supports
  device/volume prefixes, current-volume absolute paths, relative paths, `.` and
  `..`. The target must be a directory; failure preserves cwd. Children inherit
  cwd at spawn and can change it independently.
- **`getcwd`**: writes a NUL-terminated canonical path such as `boot:docs`, using
  a unique volume label or the device slot when that label is ambiguous or
  collides with another namespace name. The supplied byte capacity must include
  the terminator; invalid or undersized buffers return `-1`. Returns `0` on
  success, rather than a pointer.

- **`volume_info`**: enumerate mounted FAT32 volumes by zero-based index. Reports
  the device slot, volume label, boot-volume flag and read-only flag. Returns
  `1` for an item and `0` at the end; invalid output pointers return `-1`.
- **`assign`**: create or replace a global, single-target assign to an existing
  directory. Relative targets use the caller's cwd. A NULL target removes an
  assign; removing an unknown assign fails. `sys:` is immutable; `c:` can be
  replaced or removed. Targets are resolved snapshots, and failed replacements
  preserve the previous target. Device-slot names remain reserved.
- **`assign_info`**: enumerate assigns by zero-based index. Reports the name,
  canonical qualified target and immutable flag. Returns `1` for an item,
  `0` at the end or `-1` for invalid output pointers.

File paths for `open`, `stat`, `chdir` and explicit `spawn` paths use the same
namespace rules; `/` selects the current volume's root. Syscall numbers 14 to
31 belong to the scheduler/memory track; filesystem additions use 32 to 47.

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

#define KORA_PATH_MAX 4128  /* qualified path bytes including NUL */
#define VOLUME_BOOT 1u
#define VOLUME_READ_ONLY 2u
#define ASSIGN_IMMUTABLE 1u

struct volume_info {
    char device[8];         /* slot name without colon */
    char label[12];         /* FAT label, or empty */
    unsigned int flags;
};

struct assign_info {
    char name[32];          /* assign name without colon */
    char target[KORA_PATH_MAX];
    unsigned int flags;
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
