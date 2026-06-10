#include "arch/exception.h"
#include "console.h"
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

  fb_console_t fb_console;
  if (fb_console_init(&fb_console, 1024, 768, 32)) {
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

  // Launch the first user program in EL0.
  extern char user_hello_start[];
  void *ustack = frame_alloc();
  task_t hello = {
      .entry = (uint64_t)user_hello_start,
      .user_sp = (uint64_t)ustack + PAGE_SIZE,
      .state = TASK_RUNNABLE,
      .exit_code = 0,
  };
  printf("Launching user task: entry=0x%lx sp=0x%lx\n", hello.entry,
         hello.user_sp);
  task_run(&hello);
  printf("User task exited with code %d\n", hello.exit_code);

  console_init();
  console_run();
}
