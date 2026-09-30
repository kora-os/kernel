# Bare-Metal Kernel Roadmap

This roadmap outlines the minimum feature set needed to turn the current boot + UART prototype into a usable bare-metal console environment on Raspberry Pi hardware (and QEMU). Each milestone is intentionally high-level so we can iterate without locking in implementation details too early.

## Milestone 0 – Validate Boot Chain
- **Confirm armstub requirements**: Review Raspberry Pi 4 boot flow and decide whether the default `armstub8.bin` suffices or if we need a custom stub to select EL2/EL1, configure the MMU setup window, or hand off extra state.
- **Document firmware inputs**: Capture the set of registers/memory locations the VideoCore bootloader expects (mailboxes, entry addresses) and what state it provides when jumping into `kernel_main`.
- **Add boot diagnostics**: Extend the early UART routine to emit build identifiers and system register dumps so we can verify execution level and core state before deeper changes.

## Milestone 1 – System Register Definitions
- **Create `include/arch/system_registers.h`** (or similar) with macros/helpers for commonly accessed ARMv8 registers (e.g., `CurrentEL`, `SPSR_EL1`, `SCR_EL3`, `CNTFRQ_EL0`).
- **Provide read/write intrinsics**: Implement inline wrappers (`mrs`/`msr`) for the registers we rely on so the rest of the kernel avoids hard-coded assembly snippets.
- **Capture execution context**: Add lightweight helpers to detect the current exception level, core ID, and timing frequency for later initialization steps.

## Milestone 2 – Exception Vector Table
- **Draft vector table layout**: Build an `exception_vectors.S` with entries for synchronous, IRQ, FIQ, and SError at EL1 (and EL2 if we remain at EL2).
- **Install vector base**: Update early boot to set `VBAR_EL1` (and `VBAR_EL2`) using the new system register helpers.
- **Default handlers**: Provide minimal default handlers that log via UART and halt, validating that vector entry works before wiring real interrupt logic.

## Milestone 3 – Interrupt Controller Bring-Up
- **Select target configuration**: For QEMU (raspi3b) leverage the BCM2836-style interrupt controller; for Pi 4 use the GIC-400. Document the differences and decide on a common abstraction.
- **Initialize controller**: Map the controller registers, enable the IRQ line used by Mini UART/PL011, and unmask core interrupts.
- **Interrupt enable path**: Add helpers to toggle `DAIF` mask bits and globally enable IRQ handling after the console is ready.
- **Smoke tests**: Generate a timer or UART interrupt in QEMU to prove the vector table and controller init are correct.

## Milestone 4 – UART Console Refinement
- **Consolidate drivers**: Refactor the Mini UART and PL011 paths into a single console interface with runtime selection based on build target.
- **Buffered I/O**: Introduce a simple ring buffer or line buffer so the console survives bursty traffic and supports basic input editing.
- **Formatted logging**: Provide lightweight `printf`-style formatting (possibly a trimmed-down vsnprintf) for structured debug output.
- **Interrupt-driven RX/TX**: Optionally enable interrupt-driven receive and transmit to reduce busy-waiting once the interrupt controller is stable.

## Milestone 5 – Testing & Tooling
- **QEMU scripts**: Expand `run-qemu.sh` with flags for enabling the GDB stub (`-s -S`) and redirecting UART output to files for regression captures.
- **Hardware checklist**: Document a manual test flow (power-on, serial connection, expected boot banner, echo test) and add it to `docs/developer-guide.md`.
- **Automation hooks**: Investigate integrating a basic CI job or local script that rebuilds both variants and runs the QEMU smoke test to guard against regressions.

This plan should evolve alongside implementation progress; update the roadmap as we discover new requirements or validate assumptions. Until we lock down the interrupt and console stack, keep the focus on instrumentation and observability so debugging remains manageable.


## QEMU virt development platform

An independent AArch64 virt target supports platform discovery, PL011/GICv2,
generic timer, ramfb, VirtIO MMIO block storage and keyboard. It preserves the
Pi hardware and raspi3b targets and runs the existing FAT32/EL0 environment.
See `virt-plan.md` for the completed PR bundles and validation. Automated
profiles cover embedded/external rootfs, graphics/serial, RAM sizes, EL2 entry,
and repeated allocation/process lifetime. Scheduler and writable filesystem
work remain future subsystems; this platform provides their development target.
