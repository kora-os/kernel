#!/bin/bash
# Launch the retained raspi3b target or the independent virt development target.
set -euo pipefail
TARGET="${KORA_QEMU_TARGET:-raspi3b}"
BUILD_DIR="${BUILD_DIR:-build}"
if [[ "${1:-}" == "--virt" ]]; then TARGET=virt; shift; fi
if [[ "${1:-}" == "--raspi3b" ]]; then TARGET=raspi3b; shift; fi
case "$TARGET" in
    raspi3b) KERNEL_IMG="$BUILD_DIR/kernel8.img"; MACHINE_ARGS=(-M raspi3b) ;;
    virt) KERNEL_IMG="$BUILD_DIR/kernel-virt.img"
          MACHINE_ARGS=(-M virt,gic-version=2,highmem=off -cpu cortex-a72 -smp 1 -nic none -m "${KORA_QEMU_RAM:-256M}") ;;
    *) echo "Unsupported target: $TARGET" >&2; exit 1 ;;
esac
[[ -f "$KERNEL_IMG" ]] || { echo "Missing $KERNEL_IMG; build first." >&2; exit 1; }
DISPLAY_ARGS=(-display none)
if [[ "${KORA_QEMU_FB:-0}" == "1" ]]; then
    DISPLAY_ARGS=(-display "${KORA_QEMU_DISPLAY:-cocoa}")
fi
echo "Starting KoraOS on $TARGET; Ctrl-A X quits QEMU"
exec qemu-system-aarch64 "${MACHINE_ARGS[@]}" -kernel "$KERNEL_IMG" \
    -serial stdio "${DISPLAY_ARGS[@]}" "$@"
