#!/bin/bash
#
# Build the KoraOS FAT32 ramdisk image that is embedded into the kernel.
#
# The image is a *bare* FAT32 volume (no MBR/partition table): the BPB sits at
# LBA 0, which is the simplest thing for the in-kernel driver to parse. It is
# populated by mirroring a source directory tree (fsroot/) into the volume.
#
# Requires mtools (mformat/mmd/mcopy) -- a pure-userspace toolchain that needs
# no mounting, loop devices, or root:
#
#     brew install mtools        # macOS
#     apt-get install mtools     # Debian/Ubuntu
#
# mformat with -F forces a FAT32 BPB even for small volumes (the FAT32 >=65525
# cluster rule is a host-tool convention; the KoraOS driver only reads the BPB),
# so a 2 MiB image keeps kernel bloat negligible.

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./create-fs-image.sh [options]

Options:
  -r, --fsroot DIR   Source tree copied into the image (default: fsroot)
  -o, --output FILE  Output image path (default: build/fs/koraos.img)
  --size SIZE_MB     Image size in MiB (default: 2)
  --volume LABEL     FAT32 volume label (default: KORAOS)
  -h, --help         Show this message
EOF
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

fsroot_dir="$script_dir/fsroot"
image_path="$script_dir/build/fs/koraos.img"
size_mb=2
volume_label="KORAOS"

while [[ $# -gt 0 ]]; do
  case "$1" in
  -r | --fsroot)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --fsroot" >&2; exit 1; }
    fsroot_dir="$1" ;;
  -o | --output)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --output" >&2; exit 1; }
    image_path="$1" ;;
  --size)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --size" >&2; exit 1; }
    size_mb="$1" ;;
  --volume)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --volume" >&2; exit 1; }
    volume_label="$1" ;;
  -h | --help)
    usage; exit 0 ;;
  *)
    echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

for tool in mformat mcopy; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: '$tool' not found. Install mtools (brew install mtools / apt-get install mtools)." >&2
    exit 1
  fi
done

image_dir=$(dirname "$image_path")
mkdir -p "$image_dir"

echo "Creating ${size_mb} MiB FAT32 image at $image_path"
rm -f "$image_path"
dd if=/dev/zero of="$image_path" bs=1048576 count="$size_mb" status=none
# -F forces FAT32; -i selects the image file instead of a drive letter.
mformat -F -v "$volume_label" -i "$image_path" ::

if [[ -d "$fsroot_dir" ]] && [[ -n "$(ls -A "$fsroot_dir" 2>/dev/null)" ]]; then
  echo "Populating from $fsroot_dir"
  # -s: recurse into directories (creating them in the image); -Q: fail fast.
  mcopy -s -Q -i "$image_path" "$fsroot_dir"/* ::/
else
  echo "Note: $fsroot_dir is empty or missing; image will contain no files."
fi

echo "Contents:"
mdir -i "$image_path" -b -/ 2>/dev/null | sed 's/^/  /' || true
