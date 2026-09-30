# Filesystem

KoraOS has a **read-only FAT32 filesystem**. It is what the kernel uses to find
and load userland programs: the kernel boots `/bin/init`, which starts
`/bin/shell`, which loads `/bin/ls`, `/bin/cat`, and the rest, all as
independent binaries read from the filesystem, not embedded in the kernel image.

## Where the disk comes from

Pi hardware and raspi3b use a **ramdisk**: a FAT32 image built on the host and
embedded in the kernel. QEMU virt can instead mount one attached VirtIO MMIO
block disk. No SD/eMMC driver exists yet.

```
create-fs-image.sh  ──(mtools)──▶  build/userfs/aarch64/koraos.img  ──(.incbin)──▶  kernel image
       ▲                                                                    │
   fsroot/  +  build/userfs/aarch64/user/*.elf                                    _koraos_fs_start/_end
```

- [`create-fs-image.sh`](../create-fs-image.sh) formats a bare FAT32 volume
  (BPB at LBA 0, no MBR/partition table, the simplest thing to parse) and
  populates it. It requires **mtools** (`mformat`, `mcopy`); see the
  [developer guide](developer-guide.md).
- Everything under [`fsroot/`](../fsroot) is mirrored into the image root.
- Each built user program (`build/userfs/aarch64/user/<name>.elf`) is installed as
  `/bin/<name>` (the `.elf` suffix is stripped, so programs are spawned by bare
  name).
- The standalone userfs producer (`cmake/userfs`) runs the script once; each
  kernel configuration `.incbin`s the prepared `koraos.img` into the
  kernel's read-only data, exposing `_koraos_fs_start` / `_koraos_fs_end`. The
  image is rebuilt whenever anything in `fsroot/` or any user program changes.

Because the image is part of the kernel binary, it is always present on both
QEMU and real hardware with no extra media or QEMU flags.

## Layers

```
  task_spawn()  ──▶ loads /bin/<name>, hands bytes to elf_load()   (src/proc/task.c)
  file syscalls ──▶ open / close / read / lseek / readdir / stat   (src/sys/syscall.c)
  ─────────────────────────────────────────────────────────────────────────────
  FAT32 driver  ──▶ mount, path walk, FAT chains, LFN→UTF-8, read  (src/fs/fat32.c)
  ─────────────────────────────────────────────────────────────────────────────
  block device  ──▶ blk_read(lba, count, buf), 512-byte sectors    (src/fs/blkdev.c)
  ─────────────────────────────────────────────────────────────────────────────
  ramdisk       ──▶ the embedded FAT32 image, served in place from .rodata
```

Each layer has a clean seam:

- **Block device** ([`include/fs/blkdev.h`](../include/fs/blkdev.h)), a
  `blkdev_t` with a `read` function pointer over 512-byte sectors. Backends
  include the in-memory ramdisk and VirtIO MMIO disk; a future SD/eMMC driver can plug
  itself here later **without any change to the FAT32 code above**.
- **FAT32** ([`include/fs/fat32.h`](../include/fs/fat32.h)), a single mounted
  volume, absolute paths, directory traversal, file reads, and stat. No VFS.
- **File syscalls + per-task fd table**, see [syscalls.md](syscalls.md).

## FAT32 details

- **Mount** parses the BPB at sector 0 (bytes/sector, which must be 512,
  sectors/cluster, reserved sectors, number of FATs, `FATSz32`, `RootClus`) and
  caches the geometry.
- **Cluster chains** are followed through the FAT with a one-sector FAT cache.
- **Long filenames (LFN/VFAT)** are decoded from their on-disk **UTF-16** to
  **UTF-8**, including surrogate pairs. UTF-8 is used everywhere names cross the
  syscall boundary, it is lossless versus the UTF-16 source and keeps the whole
  byte-string / C-string userland (argv, the shell, `write`) working unchanged.
  Names can be up to 255 UTF-16 code units, i.e. up to 765 UTF-8 bytes.
- **8.3 short names** are an OEM code page, which the driver does not decode:
  non-ASCII short-name bytes become `?`. Names that genuinely need non-ASCII
  characters carry an LFN, which is decoded fully.
- **`mformat -F`** forces a FAT32 BPB even for a small (2 MiB) volume, below the
  usual 65525-cluster minimum. The driver only reads the BPB, so this is fine
  and keeps the embedded image tiny.

## Current layout of the image

```
/README.TXT
/docs/…                      example files, including Unicode-named ones
/bin/init  /bin/shell        the boot chain
/bin/ls  /bin/cat  /bin/echo  /bin/hello  /bin/gfxdemo
```

## Limitations and deferred work

- **Read-only.** No create, write, append, delete, or directory modification.
- **Pi storage is ramdisk only.** virt also supports a bare FAT32 VirtIO disk.
  No SD/eMMC driver exists.
- **No partition table.** The image is a bare FAT32 volume; MBR/GPT parsing is
  not implemented.
- **No VFS.** One filesystem, one mount, a thin file/fd layer.
- **Case-insensitive matching is ASCII-only.** Non-ASCII case folding is not
  attempted.
- **Surrogate-pair LFN decoding** is implemented to spec but is not covered by a
  test fixture, because the host `mtools` mis-encodes astral characters.

## VirtIO root disk on virt

```bash
./build.sh --target qemu_virt
KORA_QEMU_DISK=build/userfs/aarch64/koraos.img ./run-qemu.sh --target qemu_virt
```

Attach one bare FAT32 volume (BPB at sector zero, without MBR/GPT). The image
contains `/bin/init` and `/bin/shell`. The launcher selects modern VirtIO MMIO;
legacy transport is unsupported. Media defaults to read-only; set
`KORA_QEMU_DISK_READONLY=off` on a disposable image for raw block experiments.
The block API supports `blk_read`, `blk_write` and negotiated `blk_flush`.
FAT32 and file syscalls remain read-only. The driver's capacity is bounded by
the current 32-bit sector API. Missing disks use the embedded root; a configured
broken disk does not silently fall back. Timed-out devices retain DMA buffers
and reject subsequent requests until reboot.

## Shared producer and dependencies

All current boards share AArch64 userland and the syscall ABI, so one userfs
artifact serves all four kernels and Debug/Release configurations. User headers,
program sources, runtime assembly and added/removed fsroot files trigger updates.
Image generation is atomic. Kernels depend on the prepared image and reassemble
their blob when it changes. `--userfs-only` builds it alone; `--userfs-dir` consumes
an explicitly prepared artifact without rebuilding. A future userland ISA/ABI or
configuration change needs a distinct artifact, rather than sharing incompatible
binaries.
