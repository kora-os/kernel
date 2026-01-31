#!/bin/bash
# Script to run the Raspberry Pi kernel in QEMU

KERNEL_IMG="build/kernel8.img"

if [ ! -f "$KERNEL_IMG" ]; then
  echo "Error: $KERNEL_IMG not found. Build the kernel first."
  exit 1
fi

echo "Starting KoraOS in QEMU (raspi3b machine)..."
echo "Type 'Ctrl-A X' to quit QEMU"
echo ""

QEMU_DISPLAY_ARGS=("-display" "none")
if [ "${KORA_QEMU_FB:-0}" = "1" ]; then
  QEMU_DISPLAY_ARGS=("-display" "cocoa")
fi

exec qemu-system-aarch64 \
  -M raspi3b \
  -kernel "$KERNEL_IMG" \
  -serial stdio \
  "${QEMU_DISPLAY_ARGS[@]}" \
  "$@"
