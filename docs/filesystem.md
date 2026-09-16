# Filesystem

KoraOS has a **read-only FAT32 filesystem**. It is what the kernel uses to find
and load userland programs: the kernel boots `/bin/init`, which starts
`/bin/shell`, which loads `/bin/ls`, `/bin/cat`, and the rest, all as
independent binaries read from the filesystem, not embedded in the kernel image.

## Where the disk comes from

There is no SD/eMMC driver yet. The "disk" is a **ramdisk**: a FAT32 image built
on the host and embedded into the kernel image as a read-only blob.

```
create-fs-image.sh  ──(mtools)──▶  build/fs/koraos.img  ──(.incbin)──▶  kernel image
       ▲                                                                    │
   fsroot/  +  build/user/*.elf                                    _koraos_fs_start/_end
```

- [`create-fs-image.sh`](../create-fs-image.sh) formats a bare FAT32 volume
  (BPB at LBA 0, no MBR/partition table, the simplest thing to parse) and
  populates it. It requires **mtools** (`mformat`, `mcopy`); see the
  [developer guide](developer-guide.md).
- Everything under [`fsroot/`](../fsroot) is mirrored into the image root.
- Each built user program (`build/user/<name>.elf`) is installed as
  `/bin/<name>` (the `.elf` suffix is stripped, so programs are spawned by bare
  name).
- CMake runs the script at build time and `.incbin`s `koraos.img` into the
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
  `blkdev_t` with a `read` function pointer over 512-byte sectors. The only
  backend today is the in-memory ramdisk; a real SD/eMMC driver can register
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
- **Ramdisk only.** No SD/eMMC driver, the disk is baked into the kernel. A
  real driver drops in under the `blkdev_t` seam.
- **No partition table.** The image is a bare FAT32 volume; MBR/GPT parsing is
  not implemented.
- **No VFS.** One filesystem, one mount, a thin file/fd layer.
- **Case-insensitive matching is ASCII-only.** Non-ASCII case folding is not
  attempted.
- **Surrogate-pair LFN decoding** is implemented to spec but is not covered by a
  test fixture, because the host `mtools` mis-encodes astral characters.
