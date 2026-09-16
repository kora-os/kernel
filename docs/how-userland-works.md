# How Userland Works

This explains what actually happens when KoraOS runs a program: how a binary
goes from a file on the filesystem to executing code, where it lives in memory,
how several programs coexist, and — importantly — the fact that **there is no
memory protection**.

See also [writing-userland-programs.md](writing-userland-programs.md) (author's
view), [syscalls.md](syscalls.md), and [filesystem.md](filesystem.md).

## From a name to running code

When something calls `spawn("ls", …)` (the shell) or the kernel starts
`/bin/init`, the same pipeline runs, in [`src/proc/task.c`](../src/proc/task.c):

1. **Resolve the name.** A bare name becomes `/bin/<name>`; an absolute path is
   used as-is.
2. **Read the file.** The program is read off the FAT32 filesystem into a
   temporary page-aligned buffer (`frame_alloc_pages`).
3. **Load the ELF.** [`elf_load`](../src/user/elf.c) parses the buffer and lays
   the program out in memory (details below). The temporary buffer is freed
   immediately afterwards — `elf_load` has copied everything it needs out of it.
4. **Set up the task.** A one-page user stack is allocated, a task-table slot
   and pid are assigned, and `argv` is copied onto the top of the new stack.
5. **Enter EL0.** `enter_user` ([`src/arch/entry.S`](../src/arch/entry.S)) saves
   the kernel's context and drops to EL0 at the program's entry point, with
   `SP_EL0` at the stack top and `x0`/`x1` = `argc`/`argv`.

The program runs until it returns from `main` (the entry stub then calls
`exit`) or calls `exit` directly, which traps back into the kernel and unwinds
to whoever spawned it.

## Where a binary is placed

All dynamic memory comes from a single **physical page allocator**
([`src/mm/frame_alloc.c`](../src/mm/frame_alloc.c)): a fixed pool (16 MB) of
4 KB pages that begins just past the kernel image. Because the MMU uses a **flat
identity map** (see below), a physical page returned by the allocator is usable
as-is at the same address by both the kernel and EL0 — there is no separate
"map it into the process" step.

User programs are built as **position-independent executables** (`ET_DYN`), so
they can load anywhere. `elf_load`:

- finds the address span of the program's `PT_LOAD` segments,
- allocates that many contiguous pages from the pool — call the base `region`,
- computes `bias = region - link_base` and copies each segment to
  `p_vaddr + bias` (the BSS tail is already zeroed by the allocator),
- applies the `R_AARCH64_RELATIVE` relocations (each fixed-up pointer becomes
  `bias + addend`), and
- reports the absolute entry point `e_entry + bias`.

So each spawned program gets its **own distinct region** in the pool. A task
holds three kinds of memory, all from the same pool:

| Region | Size | Notes |
|--------|------|-------|
| image  | a few pages | code + data + BSS, placed by `elf_load` |
| stack  | 1 page (4 KB) | `SP_EL0` starts at the top; `argv` lives here |
| heap   | up to 64 KB | allocated lazily on the first `sbrk`, grown by the break |

When a task is reaped, all three are returned to the pool.

## Several programs at once

The process model is **cooperative, nesting, and single-core**, with **no
scheduler and no preemption**:

- `spawn` is *synchronous*: it suspends the caller, runs the child all the way
  to completion in EL0, and only then returns the child's pid. So the "process
  tree" is really a call stack — `kernel → /bin/init → /bin/shell → /bin/ls` —
  where each parent is parked, waiting for its child.
- A finished task becomes a **zombie** (its memory stays allocated so its exit
  code remains valid) until the parent reaps it with `wait`.
- Up to `MAX_TASKS` (8) tasks can be live at once — the nesting depth plus any
  unreaped zombies.

Because of the identity map, **every task's image, stack, and heap are mapped
and addressable at the same time** (there is no address-space switch between
tasks). Distinct tasks simply occupy distinct regions of the one pool. `yield`
currently does nothing — there is nothing to switch to.

## Memory model — no protection, by design

The MMU ([`src/mm/mmu.c`](../src/mm/mmu.c)) installs a **flat identity map of the
low 4 GB** with 2 MB blocks: **virtual address == physical address**,
everywhere. There are exactly three kinds of block:

| Region | Covers | EL1 | EL0 | Executable |
|--------|--------|-----|-----|------------|
| code   | kernel + statically-linked text (below `text_end`) | read-only | read-only | yes |
| normal | the rest of RAM (the page pool, kernel data/stack/heap, the page tables, loaded programs) | read/write | **read/write** | at EL0 only |
| device | MMIO at/above the peripheral base | read/write | read/write | no |

The only hardware-enforced protections are:

- the **code region is read-only**, so nothing can scribble over kernel or
  static program text; and
- EL0-writable pages are forced **PXN** (privileged-execute-never), which is why
  a program loaded into normal RAM runs at EL0 but the *kernel* cannot execute
  it — and why the kernel's own code lives in the separate read-only code block.

**Everything else is wide open, on purpose.** In particular:

> **There is no memory protection between programs, or between a program and the
> kernel.** Any EL0 program can read and write *all* of normal RAM: its own
> image, every other process's image/stack/heap, the kernel's data, stack, and
> heap, and even the MMU's own page tables. Device memory (peripherals) is
> reachable from EL0 too. There are no per-process address spaces, no guard
> pages, and no `NULL`-page trap.

This is not a limitation on the way to something stricter — **it is the model,
and it is meant to stay that way.** KoraOS is built in the spirit of the home
computers of the 1970s–90s: the Amiga, the Atari ST, the early Macintosh, the
DOS-era PC — machines with no MMU-enforced protection, where the whole system
was open to whoever was sitting in front of it. Nothing here is walled off from
you either. From a plain user program you can read and write kernel memory,
patch a running system call, poke a peripheral, or scribble over the page
tables — because that is exactly the kind of play the system is for.

The intended audience is the **user/developer** (to borrow Terry Davis'
phrase): someone who wants to *play* with the machine, understand it end to end,
and change it while it runs — not someone who needs a hardened OS to sandbox
untrusted apps. KoraOS is not trying to compete with Linux or a "professional"
OS; it deliberately sits in that older, opener space.

The flip side is the obvious one, and it is part of the fun: a single stray
pointer can take the whole system down. When that happens, you reboot and try
again — think of it as an Amiga guru meditation. Have fun, and keep a finger
near the reset button. :)
