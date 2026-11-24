# Agent Handbook

Use this file when you drop into the workspace so you can ramp quickly and keep changes aligned with project conventions.

## Project Snapshot
- **Target**: QEMU AArch64 virt (independent development target), plus Bare-metal Raspberry Pi kernel (Pi 3 and Pi 4, also via QEMU raspi3b/raspi4b)
- **Current Capabilities**: EL0/FAT32 reads/writes and file tools, volume/assign paths, Pi USB keyboards, mice and drives, Pi 3/Pi 4 SD card; SMP threads and jobs; virt PL011/GICv2/v3, ramfb, VirtIO storage/input and CI development profiles
- **Toolchain**: LLVM/Clang cross-compilation managed by CMake (`./build.sh`)

## Where To Look
- `README.md` – entry point with quick-start build/run commands.
- `docs/developer-guide.md` – detailed toolchain setup, build variants, QEMU usage, hardware deployment.
- `docs/repository-guidelines.md` – coding style, directory layout, and contribution workflow.
- `project_doc/virt-plan.md` – virt target milestones and delivery verification.
- `project_doc/roadmap.md` – milestones 6 to 12: kernel heap, SMP scheduler and threads, volumes, FAT32 write, storage drivers, hotplug, libc.
- `project_doc/console-plan.md` – proposal for the console and screen: input events, message ports, a user-space console library and a screen manager task; the base for koracli.
- `project_doc/features.yaml` – feature status list read by the project website; update it in any PR that changes a feature's status, and run `tests/check-features.py`.
- `project_doc/koracli-spec.md` – what KoraCLI is (revised in place as points settle); `docs/koracli.md` is its user reference: every KoraCLI PR updates it, with the differences between modes and dialects, and the section status (planned or working).

## Working Guidelines
- Follow the clang-flavored C style already in the tree (four-space indent, same-line braces, snake_case identifiers).
- Keep headers self-contained; prefer `const` pointers for memory-mapped peripherals.
- Build with `./build.sh --target <name>`; repeated targets select a subset, and
  `all` selects qemu_raspi3b, qemu_raspi4b, qemu_virt, hw_raspi3b and hw_raspi4b. The first
  selected kernel supplies `build/compile_commands.json` for clangd.
- Use `build.sh --target qemu_virt` and `run-qemu.sh --target qemu_virt` for
  independent development; raspi3b is `qemu_raspi3b`, raspi4b (QEMU 9.0+, no USB)
  is `qemu_raspi4b`. User programs/FS generation
  live in `cmake/userfs.cmake` and `cmake/userfs`; kernels consume shared userfs.
- User programs link the libc (`user/libc`, ISO C headers, entry point,
  malloc, stdio) and libk (`user/libk`, the KoraOS API only: what the libc
  provides is not duplicated there). `tests/run-libc-tests.sh`
  cross-builds the libc, checks its link and headers and runs its host tests;
  the plan is `project_doc/libc-plan.md`. `tests/run-user-tests.sh` runs the
  host tests of the user programs (`user/tests`) on that libc and its fake
  kernel. Programs use `stdin`, `stdout` and `stderr`, never fds 0, 1 and 2
  (`tests/check-console-fds.py`), and flush `stdout` before the console
  changes hands (libk's `spawn` does).
- FAT32 writes are developed on disposable QEMU images only: virt disks
  (`tests/run-qemu-write.py`) and raspi3b/raspi4b SD and raspi3b USB scratch
  drives from a `--storage-writable` build (`tests/run-qemu-storage-write.py`),
  both with fsck/mtools verification. SD cards and USB drives are read-only in
  normal builds. Circle runs multi-core (USB storage is called from any core);
  see `docs/locking.md`. The
  shared `build/userfs/aarch64/koraos.img` is immutable; never attach it writable
  or test writes on a Pi firmware card. Mutating user tools must surface sync
  failures. The ramdisk (`--ramdisk SIZE`) is read-write and lives in RAM only.
- Kernels built without `--ramdisk` boot from the first volume with
  `/bin/init` (SD card, USB drive; a VirtIO disk on virt), so QEMU raspi runs
  need `--ramdisk 16M` or a card/drive from `tests/create-sd-card-image.py`.
- The terminal (escape parser, grid, renderer) is the console library
  `user/libconsole`: one source built into the kernel (`KORAOS_KERNEL`, glue in
  `src/video/console_fb.c`) and linked by user programs (`condemo`). Keep it
  free of kernel calls; what it needs comes through `term_host_t`.
- Message ports (`src/ipc/port.c`, syscalls 48 to 54) carry pointers to the
  sender's memory; the kernel never touches a message and keeps an ended
  sender's memory until its held messages are replied (`docs/syscalls.md`).
- Signals (`src/proc/task.c`, syscalls 57 to 62) are Exec's: 32 bits per
  thread (8 to 31 allocatable), set by other threads, a port's arrivals
  (`port_signal`) and a child's end (`SPAWN_SIGNAL`), waited for together
  with `sig_wait`. A server waits that way, never by polling. Bit 0 is the
  break (`SIG_BREAK`): Ctrl+C, sent by a console's server (`console_run`).
- stdin, stdout and stderr are per-process handles (`struct std_handle` in
  `proc/task.h`, `<_libc/console.h>`): the kernel console, a file or a console
  handler's port, set at spawn (`spawn_std`). The libc reads them on first
  I/O (`user/libc/src/stdio/handle.c`); raw fds 0/1/2 stay the kernel console.
- Keyboards post key events, and mice (USB through Circle, VirtIO mouse and
  tablet, the debug console's `mouse`) mouse events, to the input event queue
  (`src/input`, `include/input/input.h`); only input that is bytes already
  (the terminal's status replies) calls `tty_input_push`. `int8_t` is signed:
  plain `char` is unsigned on AArch64. The adapter that turns
  events into console bytes (`src/input/input_tty.c`) is temporary: see
  `project_doc/console-plan.md`.
- A screen manager claims the input queue (`input_claim`, signalled per
  event) and the screen (`fb_claim`); the kernel gives both back to its
  console when the claimer ends. The debug console's `keylog on` traces
  events, xev style (`src/input/keylog.c`); it prints only with no spinlock
  held, so keep its notes and its printing apart.
- The screen manager is the user program `screens` (`user/screens.c`); its
  clients use `user/libscreen` (`screen_open`, `screen_damage`,
  `screen_event`, `screen_gone`). Neither side writes into the other's memory
  (no MMU to catch it): a screen's pixels are its program's, which the
  manager only reads; keys are the manager's messages on the screen's port.
  Meta+M is its hotkey. It keeps the mouse pointer, drawn over the front
  screen (hidden by typing), and sends mouse events at its position.
  init runs `screens shell` with a display (no shell without one) and starts
  it again when it ends. A program that claims the display or the keyboard
  itself borrows them first (`screens_borrow`), as `inputprobe` does.
- A console on a screen (`user/libscreen/console.c`, `newcon`, and the
  shell's own: started on the kernel console, the shell opens one and runs
  itself there) serves its programs' packets; line editing is `user/libconsole/edit.c` and key bytes
  `keys.c`, both pure (built into the kernel too, like `term.c`) and tested
  on the host (`tests/host/edit_test.c`).
- Fonts (`user/libconsole/font.c`): the built-in 8x8 font or PSF2. A
  console takes one through a `CON_FONT` packet (libk `console_font`, the
  `font` tool) and resizes (`term_resize`). The fonts in `sys:fonts` are
  Spleen's BDFs (`third_party/spleen`), converted by `scripts/bdf2psf.py`
  at build time. Cells still hold ASCII and the `TERM_GLYPH_*` symbols;
  Unicode cells come next.
- The serial line is the kernel debug console (on QEMU too). With `conlog on`
  (the default under QEMU) it carries a copy of what the consoles show, and
  the debug console neither echoes nor prompts; `type TEXT` and `key NAME`
  post key events. The smoke test drives the shell that way (`q.send` types,
  `q.debug` runs a debug console command, `q.key` presses keys).
- Use `run-qemu.sh` for rapid UART smoke tests; see the developer guide for hardware installation steps.
- Documentation lives under `docs/`; do not store temporary todos there—use `project_doc/` for roadmap-level planning instead.

## Housekeeping
- Kernel artifacts use `build/<config>/<target>/kernel.{elf,img,map}`; userfs
  uses `build/userfs/aarch64`. Script cleanup preserves unselected targets.
- Hardware installation requires explicit `--install-to`; retain firmware boot
  filenames. Prepared external userfs is never cleaned by the orchestrator.
- SD card images can be staged or created with `create-sd-image.sh` once the hardware kernel variant exists.
- Keep ARM system register definitions, exception vectors, and interrupt bring-up work in sync with the roadmap before adding new subsystems.

If you uncover missing instructions or new best practices, update this handbook alongside the relevant docs so future agents stay aligned.
