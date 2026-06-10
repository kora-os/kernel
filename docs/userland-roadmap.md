# Userland Bring-Up Roadmap

This document is a developer-facing roadmap for moving KoraOS from its current boot-and-console state to running a first minimal user process in EL0. It is intentionally written as guidance for manual implementation, not as a task breakdown for an automated agent.

The immediate target is:

- The kernel enters EL1 in a controlled way.
- The kernel creates one user address space.
- The kernel enters EL0 and runs a tiny user program.
- The user program prints `Hello from userland` and exits.

The recommended sequence is to prove EL0 execution with an embedded user image first, then add ELF loading as the next step. That keeps bring-up risk low by separating privilege/memory issues from loader issues.

## Current baseline

At the time of writing, the relevant state of the repository is:

- Early boot starts in [`src/boot.S`](../src/boot.S) and jumps directly to `kernel_main`.
- The main flow lives in [`src/kernel.c`](../src/kernel.c).
- The linker script in [`src/linker.ld`](../src/linker.ld) defines kernel image boundaries, BSS, stack, and a heap anchor.
- [`include/mm.h`](../include/mm.h) contains only basic constants and no real memory-management API.
- [`include/sysregs.h`](../include/sysregs.h) is not yet a usable ARMv8 system-register interface.
- There is no exception-vector installation, EL0 entry path, page-table code, process abstraction, syscall dispatch, or executable loader.

This means the shortest path to userland is not "implement a full OS", but "add the smallest possible set of features needed to safely enter EL0 once and return through `svc`".

## Guiding principles

- Keep the first target to a single user task, not multitasking.
- Keep the first ABI tiny: only `write` and `exit`.
- Keep the first loader simple: embedded user image first, ELF second.
- Avoid introducing scheduler complexity before EL0 execution is proven.
- Prefer clean subsystem boundaries now, because exceptions, page tables, and task setup quickly outgrow a flat `src/` layout.

## Suggested directory layout

Introduce subsystem directories before starting the work:

- `src/arch/` for exception vectors, trap entry, EL transitions, system-register helpers
- `src/mm/` for page tables, MMU enablement, and physical-page allocation
- `src/proc/` for task/process state and user-context setup
- `src/sys/` for syscall dispatch
- `src/user/` for embedded user images, ELF parsing, and test programs
- `include/arch/`, `include/mm/`, `include/proc/`, `include/sys/`, `include/user/` for matching headers

This does not need to be perfect up front, but it is worth doing before trap and MM code starts accumulating.

## Target architecture

Use the following model for the first userland milestone:

- Kernel executes in `EL1`
- User code executes in `EL0`
- One kernel address space remains active while the current user task's mappings are visible
- One user task exists at a time
- No preemption, no scheduler, no fork/exec model yet

If boot currently lands in `EL2`, add an early and explicit drop to `EL1` before normal kernel initialization.

## Milestone 1: Exception-level control and trap infrastructure

### Goal

Establish a reliable privileged execution model where the kernel runs in `EL1`, installs exception vectors, and can receive synchronous exceptions from lower EL.

### Why this is first

Without a working trap path, there is no clean way to implement syscalls. Without a stable EL1 setup, later MMU and user-mode work will be much harder to debug.

### Main work

- Replace the current placeholder system-register header with a real ARMv8 helper layer.
- Add assembly exception vectors for EL1.
- Install `VBAR_EL1` during early boot.
- If needed, add a transition from `EL2` to `EL1`.
- Add minimal exception decoding for synchronous exceptions.

### Files likely to change

- [`src/boot.S`](../src/boot.S)
- [`src/kernel.c`](../src/kernel.c)
- [`include/sysregs.h`](../include/sysregs.h) or replace it with `include/arch/sysregs.h`
- New `src/arch/exception_vectors.S`
- New `src/arch/exception.c`

### What to add

System-register helpers for at least:

- `CurrentEL`
- `VBAR_EL1`
- `ELR_EL1`
- `SPSR_EL1`
- `SP_EL0`
- `ESR_EL1`
- `FAR_EL1`
- `TTBR0_EL1`
- `TTBR1_EL1`
- `TCR_EL1`
- `MAIR_EL1`
- `SCTLR_EL1`

Trap handling support for at least:

- synchronous exception from lower EL
- synchronous exception from current EL
- default panic handlers for IRQ/FIQ/SError during bring-up

### Acceptance criteria

- The kernel can report its current EL during boot.
- Exception vectors are installed before enabling user-mode transitions.
- An intentional synchronous exception reaches the handler and prints useful diagnostics.
- `svc #0` from lower EL can eventually be distinguished from other exception classes by reading `ESR_EL1`.

## Milestone 2: Minimal memory-management subsystem

### Goal

Create just enough physical and virtual memory infrastructure to map kernel memory and one user address space.

### Why this is second

You can technically drop to EL0 without a sophisticated VM design, but a meaningful user process with isolated code and stack quickly benefits from page-table setup. Also, ELF loading becomes much cleaner once mappings exist.

### Main work

- Introduce a physical-page allocator.
- Build page-table creation and mapping helpers.
- Enable the MMU at EL1.
- Define a simple virtual-memory layout for kernel and one user task.

### Files likely to change

- [`include/mm.h`](../include/mm.h)
- [`src/linker.ld`](../src/linker.ld)
- New `include/mm/pagetable.h`
- New `src/mm/pagetable.c`
- New `src/mm/mmu.c`
- New `src/mm/frame_alloc.c`

### Recommended minimum design

For the first version, keep it simple:

- Use a bump allocator for physical pages after the kernel image end.
- Reserve a fixed amount of RAM for early experiments.
- Map kernel text/data and device memory as needed.
- Map user text as read/execute.
- Map user stack as read/write.
- Keep one page-table root per user task even if only one task exists.

You do not need yet:

- demand paging
- swapping
- shared libraries
- copy-on-write
- a sophisticated kernel heap

### Important linker/boot considerations

You will likely need linker symbols for:

- kernel start
- kernel end
- text/data boundaries
- page-table storage if statically reserved early on

[`src/linker.ld`](../src/linker.ld) already exposes `_end`, which is enough to bootstrap a simple bump allocator, but adding more explicit symbols will help keep MM code clearer.

### Acceptance criteria

- The kernel enables the MMU without immediately faulting.
- Kernel console/UART output still works after MMU enablement.
- The kernel can allocate a few physical pages and map them predictably.
- A user text page and user stack page can be mapped into a user address space.

## Milestone 3: One-task process model

### Goal

Introduce the smallest process/task abstraction needed to enter EL0 and return to the kernel.

### Why this is third

Once traps and mappings exist, the next missing piece is a structured way to represent "the currently running user execution context".

### Main work

- Define a task structure for a single user process.
- Define a trap frame or saved register structure.
- Add helpers to initialize a user context.
- Add helpers to mark task lifecycle states such as runnable and exited.

### Files likely to change

- New `include/proc/task.h`
- New `src/proc/task.c`
- New `include/arch/trapframe.h` if you want trap state separated cleanly

### Suggested first task structure

Keep the initial structure small:

- general-purpose register snapshot
- saved EL1 exception return state
- user stack pointer
- user entry point
- page-table root
- state such as `RUNNABLE` or `EXITED`
- exit status

You do not need yet:

- PID allocation
- signals
- file descriptor tables
- parent/child relations
- scheduler queues

### Acceptance criteria

- The kernel can create one task object for a user program.
- The kernel can populate an initial EL0 register frame.
- The kernel can distinguish between a runnable task and an exited task.

## Milestone 4: Minimal syscall ABI

### Goal

Provide the smallest possible kernel/user ABI to print a message and terminate cleanly.

### Recommended syscall surface

Start with exactly two syscalls:

- `write`
- `exit`

For speed, this can be a private ABI rather than a POSIX-compatible interface. A simple convention is:

- `x8` = syscall number
- `x0`, `x1`, `x2` = arguments
- `svc #0` = enter kernel
- return value in `x0`

### Suggested syscall semantics

- `write(fd, buf, len)`:
  - support only `fd == 1` initially
  - copy bytes from user memory
  - emit them through the existing console/UART path
- `exit(status)`:
  - mark the current task as exited
  - return control to a kernel loop or monitor

If you want even less surface area, you can rename `write` to something like `debug_write`. However, `write(1, ...)` is a useful stepping stone because it resembles the eventual standard shape.

### Files likely to change

- New `include/sys/syscall.h`
- New `src/sys/syscall.c`
- `src/arch/exception.c` for SVC dispatch
- [`src/console.c`](../src/console.c) or a lower-level console/UART path for output backend

### Acceptance criteria

- `svc #0` from EL0 reaches the kernel.
- The kernel decodes the syscall number and arguments.
- `write` prints a user-provided string.
- `exit` terminates the user task and returns control to the kernel.

## Milestone 5: Entering EL0 for the first time

### Goal

Transfer control from the kernel to a prepared user context using `eret`.

### Main work

- Build the first user register frame.
- Set `SP_EL0` to the user stack top.
- Set `ELR_EL1` to the user entry address.
- Set `SPSR_EL1` so `eret` returns to `EL0t`.
- Return into user mode.

### Files likely to change

- `src/arch/exception.c`
- `src/proc/task.c`
- [`src/kernel.c`](../src/kernel.c)

### Practical bring-up advice

Make this milestone observable:

- print the chosen user entry address before `eret`
- print the chosen user stack top before `eret`
- on return via syscall or fault, print ESR/FAR details

This stage is where most low-level bugs show up, so good diagnostics matter more than elegance.

### Acceptance criteria

- The kernel reaches `eret` with a valid EL0 context.
- User code executes at least one instruction in EL0.
- A syscall or deliberate trap returns into the kernel successfully.

## Milestone 6: First embedded user program

### Goal

Run a tiny user program from memory without involving ELF parsing yet.

### Why embedded first

This is the fastest way to prove:

- the EL0 transition works
- user memory mappings work
- the syscall path works
- user stack setup works

If this fails, you know the problem is not in the ELF loader.

### Recommended approach

Create a tiny freestanding AArch64 user program that:

1. invokes `write`
2. invokes `exit`

It can be written in assembly first to avoid libc and ABI surprises.

### Suggested repository placement

- `src/user/hello.S`
- `src/user/hello.ld` or a small dedicated linker script if needed
- `src/user/embed.c` or build-system glue to package the binary into the kernel image

You have a few implementation options:

- link it as a flat blob and expose start/end symbols
- convert it to an object file with embedded binary data
- build it as a tiny ELF and still load it through a trivial in-kernel copy path

For the first milestone, the exact packaging is less important than making the EL0 entry deterministic.

### Acceptance criteria

- The embedded user program runs in EL0.
- The user program prints `Hello from userland`.
- The user program exits cleanly and the kernel regains control.

## Milestone 7: Minimal ELF loader

### Goal

Replace the embedded-image shortcut with a real but intentionally small ELF loader.

### Why ELF next

ELF is well documented and a good long-term choice, but it should come after EL0 bring-up. Otherwise loader bugs and privilege/memory bugs become tangled together.

### Supported feature set for the first ELF loader

Keep the initial scope narrow:

- ELF64 only
- little-endian only
- AArch64 only
- static executables only
- `PT_LOAD` segments only
- no dynamic linker
- no relocations
- no shared objects

### Main work

- Parse the ELF header.
- Validate architecture and file type.
- Parse program headers.
- Allocate/map pages for each loadable segment.
- Copy segment data into mapped memory.
- Zero-fill any BSS tail.
- Use `e_entry` as the task entry point.

### Files likely to change

- New `include/user/elf.h`
- New `src/user/elf.c`
- `src/proc/task.c`
- `src/mm/pagetable.c`

### Acceptance criteria

- The kernel can load a statically linked AArch64 ELF image.
- Loadable segments are mapped with sane permissions.
- The ELF entry point runs in EL0.
- The same `write`/`exit` demo works when loaded through ELF.

## Milestone 8: Hardening after the first hello-world

These are useful next steps once the first user process works:

- add timer interrupts
- add a basic scheduler
- support multiple tasks
- add safer user-pointer validation
- add a simple C runtime start sequence for user programs
- add more syscalls such as `brk`, `mmap`, or simple file/console abstractions
- improve fault reporting for user crashes

These should not block the first userland milestone.

## Recommended implementation order

If you want the shortest path with the least debugging ambiguity, implement in this order:

1. Proper system-register helpers and exception vectors
2. Clean EL2-to-EL1 handoff if required
3. Synchronous exception handling with `svc` decoding
4. Basic physical-page allocator
5. Basic page-table builder and MMU enablement
6. Single-task structure and EL0 context setup
7. Minimal syscall ABI with `write` and `exit`
8. Embedded user hello-world
9. ELF loader

Do not swap steps 8 and 9 unless you explicitly want to debug the loader and EL0 entry path at the same time.

## Minimum "hello from userland" contract

For the first successful demo, the kernel only needs to guarantee:

- A user text mapping exists
- A user stack mapping exists
- `SP_EL0` is valid
- `eret` enters EL0 at the user entry point
- `svc #0` traps to EL1
- syscall `write` can access the user buffer
- syscall `exit` ends the task cleanly

Everything else can wait.

## Suggested code touchpoints in this repository

The most relevant existing files for this effort are:

- [`src/boot.S`](../src/boot.S): early boot, stack setup, first control transfer
- [`src/kernel.c`](../src/kernel.c): kernel init sequencing and eventual user-task launch point
- [`src/linker.ld`](../src/linker.ld): image layout and symbols used by memory setup
- [`include/mm.h`](../include/mm.h): current MM constants, likely to be split or expanded
- [`include/sysregs.h`](../include/sysregs.h): currently a placeholder, should become a real architecture interface
- [`src/console.c`](../src/console.c): useful as the first syscall output backend

## Risks and common traps

- Entering EL0 before exception vectors are trustworthy makes debugging painful.
- Enabling the MMU before mapping console/device memory can make diagnostics disappear.
- Trying to add a scheduler before a single task works usually slows bring-up.
- Adding ELF too early can hide simpler bugs in page-table setup or trap return state.
- Forgetting to separate user and kernel permissions in page mappings can make the first demo appear to work while masking design problems.

## Final recommendation

Treat the first user process as a bring-up exercise, not as the beginning of a complete POSIX environment. The right success metric is not "userland is designed", but "KoraOS can safely enter EL0, service `write` and `exit`, and return to the kernel with clean control flow".

Once that works, ELF is the next rational upgrade. After ELF, the system is in a much stronger position to grow toward real process management and a richer syscall surface.
