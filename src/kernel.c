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

// TEMP (Step 2): exercise the FAT32 read path end to end -- list the root
// directory, then dump a short file and a long-named file (proving LFN). Remove
// once the file syscalls and shell commands land in Step 3.
static void fat32_dump(const char *path) {
  fat32_file_t f;
  int rc = fat32_open(path, &f);
  if (rc != 0) {
    printf("fat32: open('%s') failed: %d\n", path, rc);
    return;
  }
  printf("fat32: %s (%u bytes):\n", path, f.size);
  char buf[128];
  long n;
  while ((n = fat32_read(&f, buf, sizeof(buf))) > 0) {
    for (long i = 0; i < n; i++) {
      putc(NULL, buf[i]);
    }
  }
  if (n < 0) {
    printf("\nfat32: read('%s') failed: %ld\n", path, n);
  }
}

static void fat32_selftest(void) {
  int rc = fat32_mount();
  if (rc != 0) {
    printf("fat32: mount failed: %d\n", rc);
    return;
  }
  // List a couple of directories. Names are UTF-8; the Unicode names under
  // /docs render correctly over UART (the framebuffer's ASCII font shows '?').
  const char *dirs[] = {"/", "/docs"};
  for (int i = 0; i < 2; i++) {
    fat32_file_t dir;
    if (fat32_opendir(dirs[i], &dir) != 0) {
      continue;
    }
    fat32_dirent_t de;
    printf("fat32: %s directory:\n", dirs[i]);
    while (fat32_readdir(&dir, &de) == 1) {
      printf("  %s%s (%u bytes)\n", de.name, de.is_dir ? "/" : "", de.size);
    }
  }
  fat32_dump("/README.TXT");
  fat32_dump("/docs/a-long-file-name.txt");
}

void kernel_main(void) {
  uart_init();
  uart_putc('K');
  uart_putc('\n');

  init_printf(NULL, putc);

  // Install EL1 exception vectors before doing anything that could trap.
  exception_init();

  // Enable the MMU with a flat, fully-permissive identity map, then bring up
  // the physical page allocator for later user-stack allocation.
  mmu_init();
  frame_alloc_init();

  // Bring up the ramdisk block device (embedded FAT32 image) and mount it.
  blkdev_init();
  fat32_selftest();  // TEMP (Step 2)

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

  // Start the interactive shell as the first user program. It spawns further
  // programs itself, exercising the cooperative, nesting process model.
  int pid = task_spawn("shell", 0, 0);
  int code = task_wait(pid);  // reap the shell (its parent is the kernel)
  printf("shell (pid %d) exited with code %d\n", pid, code);
  task_reap_all();  // release anything the shell left unreaped

  console_init();
  console_run();
}
