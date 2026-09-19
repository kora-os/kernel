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
  # The kernel reads keyboard input only from the serial UART, which QEMU wires
  # to this terminal. The framebuffer window is output-only (there is no USB/HID
  # keyboard driver yet), so type into THIS TERMINAL, not the QEMU window.
  echo "Framebuffer window is display-only: type into this terminal, not the window."
  echo ""
fi

exec qemu-system-aarch64 \
  -M raspi3b \
  -kernel "$KERNEL_IMG" \
  -serial stdio \
  "${QEMU_DISPLAY_ARGS[@]}" \
  "$@"
