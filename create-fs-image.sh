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
  --bindir DIR       Install DIR/*.elf into /bin (suffix stripped)
  --programs-file FILE  Install only the ELF paths listed in FILE, one per line
  -h, --help         Show this message
EOF
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

fsroot_dir="$script_dir/fsroot"
image_path="$script_dir/build/fs/koraos.img"
size_mb=2
volume_label="KORAOS"
bindir=""
programs_file=""

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
  --bindir)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --bindir" >&2; exit 1; }
    bindir="$1" ;;
  --programs-file)
    shift; [[ $# -gt 0 ]] || { echo "Missing value for --programs-file" >&2; exit 1; }
    programs_file="$1" ;;
  -h | --help)
    usage; exit 0 ;;
  *)
    echo "Unknown option: $1" >&2; usage; exit 1 ;;
  esac
  shift
done

for tool in mformat mcopy mmd; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: '$tool' not found. Install mtools (brew install mtools / apt-get install mtools)." >&2
    exit 1
  fi
done

if [[ -d "$image_path" ]]; then
  echo "error: --output must name an image file, not a directory: $image_path" >&2
  exit 1
fi
if [[ ! "$size_mb" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: --size must be a positive integer number of MiB." >&2
  exit 1
fi

image_dir=$(dirname "$image_path")
mkdir -p "$image_dir"

echo "Creating ${size_mb} MiB FAT32 image at $image_path"
# Build privately in the destination directory so a successful rename is
# atomic. Failure or interruption leaves the previously published image intact.
image_temp=$(mktemp "$image_dir/.koraos-image.XXXXXX")
trap 'rm -f -- "$image_temp"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
dd if=/dev/zero of="$image_temp" bs=1048576 count="$size_mb" status=none
# -F forces FAT32; -i selects the image file instead of a drive letter.
mformat -F -v "$volume_label" -i "$image_temp" ::

shopt -s nullglob dotglob
root_entries=("$fsroot_dir"/*)
if [[ -d "$fsroot_dir" ]] && [[ ${#root_entries[@]} -gt 0 ]]; then
  echo "Populating from $fsroot_dir"
  # -s: recurse into directories (creating them in the image); -Q: fail fast.
  mcopy -s -Q -i "$image_temp" "${root_entries[@]}" ::/
else
  echo "Note: $fsroot_dir is empty or missing; image will contain no files."
fi

# The producer supplies an exact registration manifest: stale ELF artifacts
# from removed programs must never sneak back into the published /bin tree.
# Standalone script callers may retain the --bindir glob interface.
programs=()
if [[ -n "$programs_file" ]]; then
  [[ -f "$programs_file" ]] || { echo "Missing program manifest: $programs_file" >&2; exit 1; }
  while IFS= read -r elf || [[ -n "$elf" ]]; do
    [[ -n "$elf" ]] || continue
    [[ -f "$elf" ]] || { echo "Missing registered program: $elf" >&2; exit 1; }
    programs+=("$elf")
  done < "$programs_file"
elif [[ -n "$bindir" ]]; then
  programs=("$bindir"/*.elf)
fi
if [[ ${#programs[@]} -gt 0 ]]; then
  echo "Installing registered programs into /bin"
  mmd -i "$image_temp" ::/bin 2>/dev/null || true
  for elf in "${programs[@]}"; do
    stem=$(basename "$elf" .elf)
    mcopy -Q -i "$image_temp" "$elf" "::/bin/$stem"
  done
fi

echo "Contents:"
mdir -i "$image_temp" -b -/ 2>/dev/null | sed 's/^/  /' || true

# Publish only after all formatting and copy steps succeeded.
mv -f -- "$image_temp" "$image_path"
