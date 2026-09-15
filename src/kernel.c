#include "arch/exception.h"
#include "console.h"
#include "fs/blkdev.h"
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

// TEMP (Step 1): smoke-test the ramdisk block device by reading sector 0 of the
// embedded FAT32 image and checking the BPB. Remove once the FAT32 driver lands.
static void blkdev_selftest(void) {
  uint8_t sec[BLK_SECTOR_SIZE];
  int rc = blk_read(0, 1, sec);
  if (rc != 0) {
    printf("blkdev: sector 0 read failed: %d\n", rc);
    return;
  }
  uint16_t sig = (uint16_t)(sec[510] | (sec[511] << 8));
  printf("blkdev: %u sectors, boot sig 0x%x, OEM '", blk_sector_count(), sig);
  for (int i = 3; i < 11; i++) {
    putc(NULL, (char)sec[i]);
  }
  printf("'\n");
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

  // Bring up the ramdisk block device (embedded FAT32 image).
  blkdev_init();
  blkdev_selftest();  // TEMP (Step 1)

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
