#include "arch/cxx.h"
#include "arch/exception.h"
#include "console.h"
#include "fs/blkdev.h"
#include "fs/fat32.h"
#include "mm.h"
#include "mm/frame_alloc.h"
#include "mm/mmu.h"
#include "proc/task.h"
#include "lib/printf.h"
#include "lib/stdlib.h"
#include "mini_uart.h"
#include "utils.h"
#include "video/console_fb.h"

void putc(void *p, char c) {
  if (c == '\n') {
    uart_putc('\r');
  }

  uart_putc(c);
  screen_putc(c);  // mirror kernel output to the framebuffer screen (if active)
}

void kernel_main(void) {
  uart_init();
  uart_putc('K');
  uart_putc('\n');

  init_printf(NULL, putc);

  // Run C++ global constructors now that printf is available. (Constructors
  // must not allocate yet: the frame allocator is brought up further down.)
  cxx_init();

  // Install EL1 exception vectors before doing anything that could trap.
  exception_init();

  // Enable the MMU with a flat, fully-permissive identity map, then bring up
  // the physical page allocator for later user-stack allocation.
  mmu_init();
  frame_alloc_init();

  // Prove the freestanding C++ toolchain and runtime work end to end (static
  // ctors, virtual dispatch, operator new via the frame allocator). This is
  // scaffolding for the Circle USB stack; remove once real C++ drivers land.
  cxx_selftest();

  // Bring up the ramdisk block device (embedded FAT32 image) and mount it so
  // the file syscalls have a filesystem to serve.
  blkdev_init();
  int fs_rc = fat32_mount();
  if (fs_rc != 0) {
    printf("fat32: mount failed: %d\n", fs_rc);
  }

  // Persist the screen console for the lifetime of the kernel and make it the
  // active screen, so printf output and the write/fb_info syscalls reach it.
  static fb_console_t fb_console;
  if (fb_console_init(&fb_console, 1024, 768, 32)) {
    fb_console_make_active(&fb_console);
    fb_console_write(&fb_console, "KoraOS\n");
    fb_console_write(&fb_console, "Hello from framebuffer console.\n");
  }

#if RPI_VERSION == 4
#if QEMU_TESTING
  console_log("KoraOS is running on a Raspberry Pi 4 in QEMU!\n");
#else
  console_log("KoraOS is running on a Raspberry Pi 4!\n");
#endif
#else
#if QEMU_TESTING
  console_log("KoraOS is running on a Raspberry Pi 3 in QEMU!\n");
#else
  console_log("KoraOS is running on a Raspberry Pi 3!\n");
#endif
#endif

  printf("Current EL: %d\n", get_el());

  // Start /bin/init as the first (and only) user program the kernel launches.
  // init owns userland policy from here: it spawns the shell, which spawns
  // further programs -- all loaded from the filesystem.
  int pid = task_spawn("init", 0, 0);
  if (pid < 0) {
    printf("kernel: failed to load /bin/init\n");
  } else {
    int code = task_wait(pid);  // reap init (its parent is the kernel)
    printf("init (pid %d) exited with code %d\n", pid, code);
  }
  task_reap_all();  // release anything left unreaped

  console_init();
  console_run();
}
