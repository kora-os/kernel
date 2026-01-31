#!/bin/bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage: ./create-sd-image.sh [options]

Options:
  -s, --staging DIR    Staging directory with boot files (default: build/sdcard)
  -o, --output FILE    Output image path (default: build/koraos-boot.img)
  --kernel FILE        Source kernel image (default: build/kernel8.img)
  --volume LABEL       Volume label for the FAT32 image (default: KORAOS)
  --size SIZE_MB       Force image size in megabytes (overrides auto sizing)
  -h, --help           Show this message

The script copies the freshly built kernel, creates config.txt on demand,
and assembles a FAT32 boot image suitable for Raspberry Pi Imager.
EOF
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

staging_dir="$script_dir/build/sdcard"
image_path="$script_dir/build/koraos-boot.img"
kernel_src="$script_dir/build/kernel8-hw.img"
kernel_dest_name="kernel8.img"
config_name="config.txt"
volume_label="KORAOS"
size_override_mb=""
size_margin_kb=4096
min_size_mb=8

while [[ $# -gt 0 ]]; do
  case "$1" in
  -s | --staging)
    shift
    [[ $# -gt 0 ]] || {
      echo "Missing value for --staging" >&2
      exit 1
    }
    staging_dir="$1"
    ;;
  -o | --output)
    shift
    [[ $# -gt 0 ]] || {
      echo "Missing value for --output" >&2
      exit 1
    }
    image_path="$1"
    ;;
  --kernel)
    shift
    [[ $# -gt 0 ]] || {
      echo "Missing value for --kernel" >&2
      exit 1
    }
    kernel_src="$1"
    ;;
  --volume)
    shift
    [[ $# -gt 0 ]] || {
      echo "Missing value for --volume" >&2
      exit 1
    }
    volume_label="$1"
    ;;
  --size)
    shift
    [[ $# -gt 0 ]] || {
      echo "Missing value for --size" >&2
      exit 1
    }
    size_override_mb="$1"
    ;;
  -h | --help)
    usage
    exit 0
    ;;
  *)
    echo "Unknown option: $1" >&2
    usage
    exit 1
    ;;
  esac
  shift
done

if [[ ! -f "$kernel_src" ]]; then
  echo "Kernel image not found at $kernel_src" >&2
  echo "Build the kernel first (e.g. ./build.sh --hw) before creating the SD image." >&2
  exit 1
fi

mkdir -p "$staging_dir"

echo "Staging files in $staging_dir"
cp "$kernel_src" "$staging_dir/$kernel_dest_name"

config_path="$staging_dir/$config_name"
if [[ ! -f "$config_path" ]]; then
  cat >"$config_path" <<EOF
kernel=$kernel_dest_name
arm_64bit=1
EOF
  echo "Created default $config_name"
else
  echo "Preserving existing $config_name"
fi

staging_size_kb=$(du -sk "$staging_dir" | awk '{print $1}')
if [[ -z "$staging_size_kb" || "$staging_size_kb" -lt 1 ]]; then
  staging_size_kb=1
fi

if [[ -n "$size_override_mb" ]]; then
  total_size_mb=$size_override_mb
else
  total_size_kb=$((staging_size_kb + size_margin_kb))
  total_size_mb=$(((total_size_kb + 1023) / 1024))
  if [[ "$total_size_mb" -lt "$min_size_mb" ]]; then
    total_size_mb=$min_size_mb
  fi
fi

image_dir=$(dirname "$image_path")
mkdir -p "$image_dir"

echo "Creating $total_size_mb MiB FAT32 image at $image_path"

cleanup() {
  if [[ -n "${darwin_device:-}" ]]; then
    hdiutil detach "$darwin_device" >/dev/null 2>&1 || true
    unset darwin_device
  fi
  if [[ -n "${darwin_mount:-}" ]]; then
    rm -rf "$darwin_mount" >/dev/null 2>&1 || true
    unset darwin_mount
  fi
  if [[ -n "${linux_mount:-}" ]]; then
    if command -v umount >/dev/null 2>&1; then
      ${sudo_cmd:-} umount "$linux_mount" >/dev/null 2>&1 || true
    fi
    rmdir "$linux_mount" >/dev/null 2>&1 || true
    unset linux_mount
  fi
}
trap cleanup EXIT

case "$(uname -s)" in
Darwin)
  rm -f "$image_path"
  hdiutil create -ov -size "${total_size_mb}m" -fs MS-DOS -volname "$volume_label" "$image_path" >/dev/null
  if [[ ! -f "$image_path" && -f "$image_path.dmg" ]]; then
    mv "$image_path.dmg" "$image_path"
  fi
  if [[ ! -f "$image_path" ]]; then
    echo "Failed to create disk image at $image_path" >&2
    exit 1
  fi
  darwin_mount=$(mktemp -d)
  attach_output=$(hdiutil attach -mountpoint "$darwin_mount" -nobrowse "$image_path")
  darwin_device=$(echo "$attach_output" | awk 'NR==1 {print $1}')
  if [[ -z "$darwin_device" ]]; then
    echo "Failed to attach image with hdiutil" >&2
    exit 1
  fi
  cp -a "$staging_dir/." "$darwin_mount/"
  sync
  hdiutil detach "$darwin_device" >/dev/null
  unset darwin_device
  rm -rf "$darwin_mount"
  unset darwin_mount
  ;;
Linux)
  if command -v mkfs.vfat >/dev/null 2>&1; then
    mkfs_cmd="mkfs.vfat"
  elif command -v mkfs.fat >/dev/null 2>&1; then
    mkfs_cmd="mkfs.fat"
  else
    echo "Unable to find mkfs.vfat or mkfs.fat. Install dosfstools." >&2
    exit 1
  fi

  rm -f "$image_path"
  truncate -s "${total_size_mb}M" "$image_path"
  "$mkfs_cmd" -F 32 -n "$volume_label" "$image_path" >/dev/null

  if [[ "$EUID" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
      sudo_cmd="sudo"
    else
      echo "Root privileges required to mount loop devices. Install sudo or run as root." >&2
      exit 1
    fi
  else
    sudo_cmd=""
  fi

  linux_mount=$(mktemp -d)
  ${sudo_cmd:-} mount -o loop "$image_path" "$linux_mount"
  ${sudo_cmd:-} cp -a "$staging_dir/." "$linux_mount/"
  sync
  ${sudo_cmd:-} umount "$linux_mount"
  rmdir "$linux_mount"
  unset linux_mount
  ;;
*)
  echo "Unsupported platform: $(uname -s)" >&2
  exit 1
  ;;
esac

trap - EXIT

echo "SD card image ready at $image_path"
