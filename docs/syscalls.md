# System Calls

KoraOS has a small, KoraOS-private syscall ABI. The calls are *shaped* like
their POSIX namesakes to ease a future libc shim, but the numbering is our own.

## Calling convention

`x8` holds the syscall number; arguments go in `x0` to `x3` (AAPCS order);
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
| 1 | `exit` | `void exit(int status)` | does not return (ends every thread) |
| 2 | `read` | `ssize_t read(int fd, void *buf, size_t len)` | bytes read, `0` at EOF, or `-1` |
| 3 | (retired) | was `sbrk` | `-1` |
| 4 | `spawn` | `int spawn_flags(const char *name, int argc, char *const argv[], int flags)` | child pid, or `-1` |
| 5 | `wait` | `int waitpid(int pid, int *code, int flags)` | child pid, `0` (`WNOHANG`, none yet), or `-1` |
| 6 | `getpid` | `int getpid(void)` | the program's pid (the same in every thread) |
| 7 | `yield` | `void yield(void)` | `0`, after letting other runnable tasks run |
| 8 | `fb_info` | `int fb_info(struct fb_info *out)` | `0`, or `-1` |
| 9 | `open` | `int open(const char *path, int flags)` | fd (≥ 3), or `-1` |
| 10 | `close` | `int close(int fd)` | `0`, or `-1` |
| 11 | `lseek` | `long lseek(int fd, long offset, int whence)` | new offset, or `-1` |
| 12 | `readdir` | `int readdir(int fd, struct dirent *out)` | `1` entry, `0` end, `-1` error |
| 13 | `stat` | `int stat(const char *path, struct stat *out)` | `0`, or `-1` |
| 14 | `alloc_pages` | `void *alloc_pages(size_t count)` | base of `count` zeroed pages, or `NULL` |
| 15 | `free_pages` | `int free_pages(void *base)` | `0`, or `-1` |
| 16 | `msleep` | `void msleep(unsigned long ms)` | `0`, after at least `ms` milliseconds |
| 17 | `forbid` | `int forbid(void)` | new forbid depth |
| 18 | `permit` | `int permit(void)` | new forbid depth, or `-1` if not forbidden |
| 19 | `task_info` | `int task_info(unsigned index, struct task_info *out)` | `1` entry, `0` past the last, `-1` bad pointer |
| 20 | `thread_new` | `int thread_new(void (*start)(void), thread_fn fn, void *arg, int core)` | thread id, or `-1` (use libk's `thread_create`) |
| 21 | `thread_exit` | `void thread_exit(long value)` | does not return |
| 22 | `thread_join` | `int thread_join(int tid, long *value)` | `0`, or `-1` |
| 23 | `kill` | `int kill(int pid)` | `0`, or `-1` if there is no such running program |
| 24 | `sysinfo` | `int sysinfo(struct sysinfo *out)` | `0`, or `-1` bad pointer |
| 32 | `chdir` | `int chdir(const char *path)` | `0`, or `-1` |
| 33 | `getcwd` | `int getcwd(char *buf, size_t size)` | `0`, or `-1` |
| 34 | `volume_info` | `int volume_info(unsigned int index, struct volume_info *out)` | `1` item, `0` end, `-1` error |
| 35 | `assign` | `int assign(const char *name, const char *target)` | `0`, or `-1` |
| 36 | `assign_info` | `int assign_info(unsigned int index, struct assign_info *out)` | `1` item, `0` end, `-1` error |
| 37 | `sync` | `int sync(void)` | `0`, or `-1` |
| 38 | `unlink` | `int unlink(const char *path)` | `0`, or `-1` |
| 39 | `mkdir` | `int mkdir(const char *path)` | `0`, or `-1` |
| 40 | `rmdir` | `int rmdir(const char *path)` | `0`, or `-1` |
| 41 | `rename` | `int rename(const char *old_path, const char *new_path)` | `0`, or `-1` |
| 48 | `port_create` | `int port_create(const char *name)` | port id (> 0), or `-1` |
| 49 | `port_delete` | `int port_delete(int port)` | `0`, or `-1` |
| 50 | `port_find` | `int port_find(const char *name)` | port id, or `-1` |
| 51 | `port_put` | `int port_put(int port, void *msg, int reply_port)` | `0`, or `-1` |
| 52 | `port_get` | `int port_get(int port, struct port_message *out)` | `1` message, `0` none, `-1` error |
| 53 | `port_wait` | `int port_wait(int port, struct port_message *out)` | `1` message, or `-1` |
| 54 | `port_reply` | `int port_reply(void *msg)` | `0`, or `-1` |
| 55 | `std_handles` | `int std_handles(struct std_handle out[3])` | `0`, or `-1` |
| 56 | `spawn_std` | `int spawn_std(const char *name, int argc, char *const argv[], int flags, const struct std_handle std[3])` | child pid, or `-1` |
| 57 | `sig_alloc` | `int sig_alloc(int bit)` | the bit, or `-1` |
| 58 | `sig_free` | `int sig_free(int bit)` | `0`, or `-1` |
| 59 | `sig_wait` | `unsigned sig_wait(unsigned mask)` | the signals received of `mask`, now cleared |
| 60 | `sig_set` | `unsigned sig_set(unsigned values, unsigned mask)` | the signals before the change |
| 61 | `sig_send` | `int sig_send(int tid, unsigned mask)` | `0`, or `-1` if there is no such thread |
| 62 | `port_signal` | `int port_signal(int port, int bit)` | `0`, or `-1` |
| 63 | `sig_on_end` | `int sig_on_end(int pid, int bit)` | `0`, or `-1` |
| 64 | `input_claim` | `int input_claim(int bit)` | `0`, or `-1` if another thread holds it |
| 65 | `input_read` | `int input_read(struct input_event *events, int max)` | events taken, or `-1` without the claim |
| 66 | `input_focus` | `int input_focus(int pid)` | `0`, or `-1` without the claim |
| 67 | `fb_claim` | `int fb_claim(int on)` | `0`, or `-1` if another program holds the screen |
| 68 | `gettid` | `int gettid(void)` | the calling thread's id, the pid in a program's main thread |
| 69 | `conlog` | `long conlog(const char *text, size_t length)` | bytes copied to the serial line, `0` with the debug console's conlog off |

## Notes per call

- **`write`**: fd 1 and 2 go to the kernel console, whatever the program's
  standard handles are (the libc writes to those itself; see below). A writable file
  descriptor at fd 3 or above writes at its cursor; append selects current EOF
  for every call. Returns accepted bytes, possibly a short write on exhaustion,
  or `-1`. `sync` reports durable completion.
- **`read`**: `fd` 0 reads raw bytes from the kernel console: it blocks until at least one
  arrives, then returns those already queued, up to `len`. Nothing is echoed,
  buffered into lines or translated (Enter arrives as `\r`, cursor keys as VT
  escape sequences). Line editing is a console handler's
  (`user/libscreen/console.c`). `fd ≥ 3` reads from an open file.
- **`alloc_pages`** / **`free_pages`**: Amiga-style memory: a run of `count`
  contiguous, zeroed 4 KB pages (`KORAOS_PAGE_SIZE`) anywhere in RAM, returned
  by its base address. The kernel records each run against the calling
  program (any of its threads may free it), so `free_pages` needs no size,
  rejects addresses the program does not own, and everything still allocated
  is reclaimed when the program is reaped. libk's
  `malloc` builds on these (see
  [writing-userland-programs.md](writing-userland-programs.md)). There is no
  `sbrk`: with one flat address space, memory after a heap is usually someone
  else's, so a contiguous break cannot grow reliably.
- **`exit`**: ends the whole program, whichever thread calls it (returning
  from `main` calls it). Its other threads are marked killed and die on their
  own cores (see `kill`); the program is finished, and can be reaped, once the
  last of them is gone.
- **`spawn`**: creates a process for the program, with one (main) thread. Without `SPAWN_NOWAIT` (flags
  0) it blocks the caller until the child has exited, then returns the child's
  pid; with `SPAWN_NOWAIT` it returns the pid at once and the child runs
  alongside (a background job). Either way, reap the child with `waitpid`.
  `SPAWN_CORE(n)` runs the child on core `n` for life (it fails if that core
  does not exist or is offline); without it the kernel puts the child on the
  online core with the fewest tasks. `SPAWN_SIGNAL(bit)` sets signal `bit` of
  the calling thread when the child ends (see Signals below); it fails if the
  thread has not allocated the bit. libk's `spawn(name, argc, argv)` is the
  flags-0 form. `name` is resolved to a filesystem path: a bare
  name is looked up through `c:` (initially `sys:bin`), with no fallback to
  `/bin` if the assign or command is missing; other names use the caller's
  volume-aware path resolver (see
  [filesystem.md](filesystem.md)). `argv` entries are copied onto the child's
  stack and delivered as `main(argc, argv)`.
- **`wait`**: reaps an exited child, `pid` or any child for `-1`, storing its
  exit code in `*code` (which may be `NULL`) and returning its pid. It blocks
  until one exits, unless `WNOHANG` is set, in which case it returns `0` if
  none has. `-1` means the caller has no such child. Children belong to the
  program, so any of its threads may reap them. libk's `wait(pid)`
  returns the exit code directly. A child whose parent exits first becomes an
  orphan and is reaped by the kernel.
- **`task_info`**: the `index`-th live task, one per thread (0, 1, ...): the
  program's pid, its parent pid (0 for the kernel or an orphan), core, state
  (`TASK_RUNNING`, `TASK_READY`, `TASK_SLEEPING`, `TASK_ZOMBIE` for an exited
  thread), name, and the thread id (`tid`, equal to `pid` for the main
  thread). Each call is consistent; tasks may come and go between calls.
  `/bin/ps` prints the list.
- **`thread_new`**: starts a thread of the calling program at `start` (in
  EL0) with `x0 = fn` and `x1 = arg`, on a fresh 16 KB stack, on `core` for life
  (`-1`: the online core with the fewest tasks). It shares everything else with
  the program: memory, heap, open files, cwd. libk's `thread_create(fn, arg,
  core)` passes its own `start`, which calls `fn(arg)` and hands the result to
  `thread_exit`. Fails for a missing or offline core, when the task table
  (32 tasks system-wide) or memory runs out, or while the program is exiting.
- **`thread_exit`**: ends the calling thread with `value` for `thread_join`.
  The last thread to end finishes the program, with exit code 0 unless `exit`
  or `kill` set one.
- **`thread_join`**: waits until thread `tid` of the calling program has
  ended, stores its value in `*value` (which may be `NULL`) and frees it. Each
  thread can be joined once; `-1` for an unknown thread, one of another
  program, the caller itself, or one already joined. Unjoined threads are
  freed with their program.
- **`kill`**: ends program `pid` as if it had called `exit(EXIT_KILLED)`
  (`-9`). Nothing interrupts another core: each thread notices on its own
  core, on its next syscall or on its way back to EL0 after its tick, and a
  thread blocked in `read`, `msleep`, `wait` or `thread_join` is woken to give
  up its call. A forbidden thread keeps the BKL, so `kill` waits until it
  permits. There are no signals: the program cannot catch it. Its parent
  still reaps it. The shell's `kill <pid>` uses it.
- **`sysinfo`**: the machine at a glance: cores online (use cores `0` to
  `cpus - 1` with `SPAWN_CORE` and `thread_create`) and the most the kernel
  manages, the tick rate and ticks since boot, the page size and the page
  pool's total and free pages, and live tasks and programs (zombies included)
  with their limits.
- **`msleep`**: blocks for at least `ms` milliseconds: its deadline is a time
  on the generic timer, checked on every tick of the task's core, so it wakes
  up to one 10 ms tick after that (never early, even if a tick interrupt was
  late); other tasks run meanwhile. `msleep(0)` yields.
- **`forbid`** / **`permit`**: after AmigaOS's `Forbid()`/`Permit()`. `forbid`
  keeps the big kernel lock across returns to EL0 until the matching `permit`,
  so no other core can be in the kernel meanwhile, and stops this task being
  preempted (interrupts still run). They nest; blocking breaks the forbid only
  while the task sleeps; exiting ends it. See [locking.md](locking.md).
- **`open`**: accepts one access mode (`O_RDONLY`, `O_WRONLY`, `O_RDWR`) and
  create/exclusive/truncate/append flags below. Exclusive requires create;
  truncate and append require writable access. Create alone preserves an
  existing file and may create a read-only handle on writable media. Directories
  accept only plain `O_RDONLY`. The kernel reserves a descriptor before
  creating or truncating, so a full table cannot mutate the file. fds 0/1/2
  are the console; real files start at 3.
- **`lseek`**: `whence` is `SEEK_SET` / `SEEK_CUR` / `SEEK_END`; the new offset
  must land within `[0, size]`. Not valid on directory fds.
- **`readdir`**: returns one entry per call from a directory fd. Skips deleted
  entries, the volume label, and `.` / `..`. `name` is UTF-8.
- **`stat`**: reports size and whether the path is a directory.
- **`chdir`**: changes only the calling program's current directory. Supports
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

- **`sync`**: flushes every writable mounted volume, including cached data,
  allocation tables, directory metadata and FSInfo. Read-only volumes are
  skipped. It attempts later volumes even if an earlier one failed and returns
  `-1` if any flush failed. Closing a descriptor does not imply sync.
- **`unlink`**: removes a file and its clusters. Directories, read-only files
  and live handles are rejected.
- **`mkdir`**: creates one directory with `.` and `..` entries. Its parent must
  exist; it does not create a chain of missing parents.
- **`rmdir`**: removes an empty directory. Root, cwd/assign pins, live directory
  handles and nonempty directories are rejected.
- **`rename`**: renames within one volume. Existing destination entries are
  preserved, apart from a case-only update of the same source. Open handles,
  cwd/assign pins, cross-volume moves and moving a directory under itself fail.

File I/O validates descriptor permissions even for zero bytes. Valid file
zero-byte calls may use a NULL buffer; console output retains its pointer
checks. File lengths above `UINT32_MAX`, invalid buffers and invalid flags
return `-1`. The kernel API retains detailed errors; user syscalls map them to
`-1`.

File paths for `open`, `stat`, `chdir` and explicit `spawn` paths use the same
namespace rules; `/` selects the current volume's root. Syscall numbers 14 to
31 belong to the scheduler/memory track; filesystem additions use 32 to 47;
message ports, standard handles, signals and the console work use 48 and up.

### Message ports (48 to 54)

Ports follow AmigaOS Exec (`src/ipc/port.c`, `include/ipc/port.h`). A port is
a queue owned by one program; a message is a block of the sender's own memory,
which the kernel queues by address and never reads or writes.

- **`port_create`** makes a port owned by the calling program. With a name (up
  to 15 bytes, unique, case-sensitive) others can find it; `NULL` or `""` makes
  an anonymous one, such as a reply port. At most 32 ports exist at a time.
- **`port_find`** returns the id of a named port. An id carries a generation:
  once its port is deleted, the id names nothing, even when the slot is reused.
- **`port_put`** queues `msg` on any port. `reply_port` must be one of the
  sender's own ports, or `0` for no reply. The same message cannot be in flight
  twice; at most 128 messages are in flight system-wide.
- **`port_get`** and **`port_wait`** take the oldest message from one of the
  caller's ports into `struct port_message`: the message, its kind and the
  sender's pid. `port_wait` sleeps until there is one; it returns `-1` if the
  port is deleted meanwhile or the program is killed.
- **`port_reply`** hands back a `PORT_MESSAGE` the caller took. It goes to its
  reply port as a `PORT_REPLY`, or is simply released. Until then the receiver
  may read and write the message; after that, neither it nor anyone else but
  the sender may touch it.
- **When a program ends** its ports are deleted, and the messages queued on
  them go back to their senders as `PORT_RETURNED`; so do the messages it had
  taken and not replied. Its own messages still queued elsewhere are withdrawn.
  Those a receiver holds keep the program's memory, and its process slot,
  until they are replied: a receiver never touches freed memory. Meanwhile the
  program counts in `sysinfo().procs` but cannot be found, waited for or
  killed.

### Signals (57 to 62)

Signals follow AmigaOS Exec (`Wait()`, `Signal()`, `AllocSignal()`), in the
scheduler (`src/proc/task.c`, `include/proc/task.h`). Each thread has 32
signal bits. Other threads, its ports and the end of its children set them,
and it sleeps until any of several is set: that is how one thread waits on
several ports, a port and a child, or a port and another thread at once.

- **`sig_alloc`** allocates bit `bit` of the calling thread, or for `-1` its
  highest free one, and clears it. Bits 0 to 7 are kept for the system, so a
  thread has 24. Bit 0 is the break (`SIG_BREAK` in libk): Ctrl+C on a
  console, which the program serving the console sends to the program it
  runs when that program asked for it (`console_run`); the shell does, and
  ends its foreground program. The others are not used yet.
- **`sig_free`** frees an allocated bit and clears it; a port tied to it stops
  setting it, the thread's end watches on it (`sig_on_end`) stop, and an input
  claim on it is given back.
- **`sig_wait`** sleeps until a signal in `mask` is set, then clears those of
  `mask` that are set and returns them. It returns at once if one is set
  already, and returns `0` for an empty mask. A killed program dies in it.
- **`sig_set`** sets the signals in `mask` to those of `values` and returns
  what all of them were before: `sig_set(0, 0)` only looks, `sig_set(0, m)`
  clears `m`.
- **`sig_send`** sets signals of any live thread, of any program, waking it if
  it waits for one. A thread id is the pid for a main thread, and the value
  `thread_create` returns otherwise.
- **`port_signal`** ties one of the program's ports to bit `bit` of the
  calling thread: every message arriving on it (a message, a reply or a
  returned one) sets the bit, and so does one already waiting when it is
  tied. `-1` unties it. A port has one signal; tying it again replaces it.

A signal records that something happened, not how often: set twice before a
wait it is returned once. So after a port's signal, take every message on it
(`port_get` until `0`), and expect to find none at times, since `port_wait`
and `port_get` take messages without clearing the signal. Signals are
protected by the scheduler's lock, which interrupt handlers take: the input
queue signals its claimer from the keyboard's interrupt (below).

### Screen managers (63 to 67)

A screen manager takes the screen and the keyboard from the kernel's console
and gives them to programs of its own, Amiga style (console step 4b,
[`project_doc/console-plan.md`](../project_doc/console-plan.md)). The kernel
gives both back to its console when the program that took them ends, crashed
or not. Nobody is checked: as
everywhere in KoraOS, any program may take them.

- **`sig_on_end`** sets bit `bit` of the calling thread when process `pid`
  ends, any process and not only a child (`SPAWN_SIGNAL` is that for one's
  own), once. Watching it again changes the bit, `-1` stops. It fails for a
  bit not allocated, a process already ended or never started, or a full
  table (64 watches in all). A screen manager watches its clients this way.
- **`input_claim`** makes the calling thread the input queue's consumer:
  every key event queued from then on sets bit `bit` (set at once if events
  are waiting, which are now the thread's), and the kernel console reads no
  keys. Claiming again changes the bit; `-1` gives the queue back. The claim
  is given back too when the thread ends or frees the bit; the console then
  takes the events left.
- **`input_read`** takes up to `max` (1 to 32) events, oldest first, and
  returns how many, `0` for none: it never sleeps, `sig_wait` does that.
- **`input_focus`** tells the kernel which process the claimer sends the
  events on to (the front screen's owner), or `0` for none. It is for the
  debug console's `keylog` only.
- **`fb_claim(1)`** hands the screen to the calling program, which then draws
  into `fb_info`'s framebuffer itself: the kernel console keeps taking text
  but draws nothing. `fb_claim(0)`, or the program's end, gives it back, and
  the console redraws all of it with what was printed meanwhile. Claiming it
  twice is once.

An event (`struct input_event`, below) is what a key did: down or up, its
position as a USB HID usage (`KEY_*`, whatever the layout prints on the key),
the modifiers after it (`INPUT_MOD_*`, Meta being `INPUT_MOD_*_GUI`), the
character it types under the layout, and the device that sent it. The kernel
gives it no meaning.

The debug console's `keylog on` traces every event as it is taken, in the
manner of X11's `xev`: the device, who took it (the console with the program
reading it, or the claimer and its focus) and the key.

```
KeyDown from usb-keyboard to inputprobe (pid 107) -> front: inputprobe (pid 107)
    time 25912 ms, keycode 0x10 (M), modifiers 0x0008 (LGUI)
    char U+006D "m", repeat no
```

### Standard handles (55 and 56)

A program's stdin, stdout and stderr are handles, as AmigaOS's `pr_CIS` and
`pr_COS`: each is the kernel console (`STD_CONSOLE`), one of the program's open
files (`STD_FILE`, value = its fd) or a message port that a console handler
serves (`STD_PORT`, value = the port id). The structs and the handler protocol
are in [`<_libc/console.h>`](../user/libc/includes/_libc/console.h), which
`koraos.h` includes.

- **`std_handles`** fills `out[0..2]` with the caller's handles.
- **`spawn_std`** is `spawn` with the child's handles, which must be the
  caller's own fds and ports (libk's wrapper flushes `stdout` first and packs
  the arguments into a `struct spawn_request`, the call's one argument). A
  plain `spawn` passes the caller's own handles on. Files are duplicated into
  the child with their cursors, so either side may close its copy; one file for
  several handles is one fd in the child. It fails for a file the caller has
  not open (or a directory), or a port that does not exist. The kernel gives
  its own children (`init`) the console.
- **The libc** asks for the handles on its first I/O. A console's stream
  behaves as before; a file's stdout is fully buffered; a handler's stream
  sends it one `struct con_packet` per read or write (`CON_READ`, `CON_WRITE`,
  a buffer, a length) on a reply port of its own, and takes the `result` the
  handler fills in before replying, as AmigaOS DOS packets. `CON_MODE`
  (`length` = `CON_COOKED` or `CON_RAW`) switches a console's input mode:
  libk's `console_mode()` sends it for stdin. `isatty(0..2)`
  (libk) says whether a standard stream is a console, the kernel's or a
  handler's. Raw `read(0)` and `write(1)` still reach the kernel console.

## Structs and constants

From [`user/libk/koraos.h`](../user/libk/koraos.h) (mirrored kernel-side in
`src/sys/syscall.c`, keep the layouts identical):

```c
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0x100
#define O_TRUNC 0x200
#define O_APPEND 0x400
#define O_EXCL 0x800
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

#define PORT_MESSAGE 1     /* someone's message: handle it, then port_reply() */
#define PORT_REPLY 2       /* one of ours, back from its receiver */
#define PORT_RETURNED 3    /* one of ours, back unhandled */

#define STD_CONSOLE 0      /* <_libc/console.h> */
#define STD_FILE 1         /* value: an open fd */
#define STD_PORT 2         /* value: a console handler's port */

struct std_handle {
    int kind;
    int value;
};

struct spawn_request {     /* spawn_std()'s one argument */
    const char *name;
    int argc;
    int flags;
    char *const *argv;
    struct std_handle std[3];
};

#define CON_WRITE 1
#define CON_READ 2
#define CON_MODE 3         /* length: CON_COOKED (0) or CON_RAW (1) */

struct con_packet {        /* the libc to a console handler */
    int op;                /* CON_WRITE or CON_READ */
    long result;           /* the handler's answer: bytes, 0 at end, or -1 */
    void *buf;
    unsigned long length;
};

struct port_message {      /* filled by port_get() and port_wait() */
    void *msg;
    int   kind;            /* PORT_MESSAGE, PORT_REPLY or PORT_RETURNED */
    int   sender;          /* pid of the program that sent it */
};

#define INPUT_EVENT_KEY_DOWN 1
#define INPUT_EVENT_KEY_UP 2
#define INPUT_EVENT_MOUSE 3      /* a mouse's report */
#define INPUT_FLAG_REPEAT 0x01   /* key down: an auto-repeat */
#define INPUT_FLAG_ABSOLUTE 0x02 /* mouse: x, y are a position, 0..INPUT_ABS_MAX */
#define INPUT_ABS_MAX 32767
#define INPUT_BUTTON_LEFT 0x01   /* then RIGHT 0x02, MIDDLE 0x04, SIDE 0x08, EXTRA 0x10 */
#define INPUT_MOD_LEFT_GUI 0x0008  /* and the other INPUT_MOD_* of koraos.h */

struct input_event {       /* filled by input_read() */
    unsigned char  type;   /* INPUT_EVENT_* */
    unsigned char  flags;  /* INPUT_FLAG_* */
    unsigned short modifiers;  /* INPUT_MOD_* after the event (a mouse's: the
                                  keyboard's, as of the last key event) */
    unsigned int   time_ms;    /* uptime when it was queued */
    union {
        unsigned short keycode;    /* a USB HID usage (KEY_*) */
        short          x;          /* mouse: the movement right, or the position */
    };
    unsigned char  source;     /* the device that sent it */
    union {
        unsigned char  reserved;
        unsigned char  buttons;    /* mouse: INPUT_BUTTON_* held after it */
    };
    union {
        unsigned int   codepoint;  /* key down: the Unicode character, or 0 */
        struct {
            short       y;         /* mouse: the movement down, or the position */
            signed char wheel;     /* mouse: the wheel's turn, away from you > 0 */
            unsigned char unused;
        };
    };
};

/* A mouse reports movements, in its own units; a tablet (QEMU's) positions,
   with INPUT_FLAG_ABSOLUTE. The pointer's position on the display is the
   consumer's to keep: the screen manager's. */

struct fb_info {           /* filled by fb_info() */
    unsigned long addr;    /* framebuffer base (physical == virtual) */
    unsigned int  width;
    unsigned int  height;
    unsigned int  pitch;   /* bytes per row */
    unsigned int  bpp;     /* bits per pixel */
};
```

## Adding a syscall

1. Add the number to `include/sys/syscall.h`, `user/libk/abi.h` and
   `user/libc/src/internal/syscall_abi.h` (`tests/check-syscall-abi.py`
   checks that all three agree).
2. Add an assembly stub in `user/libk/syscall.S` (`SYSCALL name, SYS_NAME`).
3. Add the C prototype (and any structs) to `user/libk/koraos.h`.
4. Implement `sys_<name>` and add a `case` in `syscall_handle` in
   `src/sys/syscall.c`. Validate user pointers with `uptr_ok`.
