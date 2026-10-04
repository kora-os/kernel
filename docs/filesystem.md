# Filesystem

KoraOS has a **read-only FAT32 filesystem**. It is what the kernel uses to find
and load userland programs: the kernel boots `/bin/init`, which starts
`/bin/shell`, which loads `/bin/ls`, `/bin/cat`, and the rest, all as
independent binaries read from the filesystem, not embedded in the kernel image.

## Where the disk comes from

Pi hardware and raspi3b use a **ramdisk**: a FAT32 image built on the host and
embedded in the kernel. QEMU virt can instead mount one attached VirtIO MMIO
block disk. Both backends register together; FAT32 mounts every valid partition and uses
the preferred boot partition as the initial working volume. No SD/eMMC driver exists yet.

```
create-fs-image.sh  ──(mtools)──▶  build/userfs/aarch64/koraos.img  ──(.incbin)──▶  kernel image
       ▲                                                                    │
   fsroot/  +  build/userfs/aarch64/user/*.elf                                    _koraos_fs_start/_end
```

- [`create-fs-image.sh`](../create-fs-image.sh) formats a bare FAT32 volume
  (BPB at LBA 0, no MBR/partition table) and
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
  task_spawn()  ──▶ resolves program paths, hands bytes to elf_load() (src/proc/task.c)
  file syscalls ──▶ open / read / stat / chdir / getcwd           (src/sys/syscall.c)
  namespace     ──▶ device / volume names and per-task cwd       (src/fs/namespace.c)
  ─────────────────────────────────────────────────────────────────────────────
  FAT32 driver  ──▶ mount, path walk, FAT chains, LFN→UTF-8, read  (src/fs/fat32.c)
  ─────────────────────────────────────────────────────────────────────────────
  block device  ──▶ blk_read(lba, count, buf), 512-byte sectors    (src/fs/blkdev.c)
  ─────────────────────────────────────────────────────────────────────────────
  ramdisk       ──▶ the embedded FAT32 image, served in place from .rodata
```

Each layer has a clean seam:

- **Block device** ([`include/fs/blkdev.h`](../include/fs/blkdev.h)), a
  `blkdev_t` with read/write/flush operations over 512-byte sectors. A registry
  holds up to eight physical backends and four FAT32 primary partitions per
  backend. Ramdisk and VirtIO register with an explicit backend type; future
  SD/eMMC and USB backends use the same registration API. Each partition exposes
  a bounded `blkdev_t` view whose LBA zero is its first sector. Bounds checks
  happen before transport I/O, so reads and writes cannot cross a partition.
- **FAT32** ([`include/fs/fat32.h`](../include/fs/fat32.h)), per-volume state
  allocated from the kernel heap, independent geometry and read
  caches, directory traversal, file reads, and stat. Each open handle retains
  its volume, so changing cwd does not redirect existing descriptors. No VFS.
- **File syscalls + per-task fd table**, see [syscalls.md](syscalls.md).

## FAT32 details

- **Mount** parses the BPB at sector 0 (bytes/sector, which must be 512,
  sectors/cluster, reserved sectors, number of FATs, `FATSz32`, `RootClus`) and
  validates geometry against the partition bounds and FAT capacity before
  caching it. Corrupt or looping chains report errors instead of hanging.
- **Cluster chains** are followed through the FAT with a one-sector FAT cache.
- **Long filenames (LFN/VFAT)** are decoded from their on-disk **UTF-16** to
  **UTF-8**, including surrogate pairs. UTF-8 is used everywhere names cross the
  syscall boundary, it is lossless versus the UTF-16 source and keeps the whole
  byte-string / C-string userland (argv, the shell, `write`) working unchanged.
  Malformed, incomplete or checksum-invalid LFN sequences fall back to their
  short-name entry.
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
- **Pi storage is ramdisk only.** virt supports bare FAT32 and primary MBR
  FAT32 partitions on a VirtIO disk.
  No SD/eMMC driver exists.
- **Primary MBR only.** GPT and extended/logical partitions are unsupported.
  The generated shared userfs remains a bare FAT32 volume.
- **No VFS.** FAT32 volumes share a namespace and a thin file/fd layer.
- **Case-insensitive matching is ASCII-only.** Non-ASCII case folding is not
  attempted.
- **Surrogate-pair LFN decoding** is implemented to spec but is not covered by a
  test fixture, because the host `mtools` mis-encodes astral characters.

## VirtIO root disk on virt

```bash
./build.sh --target qemu_virt
KORA_QEMU_DISK=build/userfs/aarch64/koraos.img ./run-qemu.sh --target qemu_virt
```

Attach a bare FAT32 volume or a disk with primary MBR FAT32 partitions. The
first supported FAT32 partition is the boot root and must contain `/bin/init`
and `/bin/shell`. The launcher selects modern VirtIO MMIO;
legacy transport is unsupported. Media defaults to read-only; set
`KORA_QEMU_DISK_READONLY=off` on a disposable image for raw block experiments.
The block API supports `blk_read`, `blk_write` and negotiated `blk_flush`.
FAT32 and file syscalls remain read-only. The driver's capacity is bounded by
the current 32-bit sector API. Missing disks use the embedded root; a configured
broken disk does not silently fall back. Timed-out devices retain DMA buffers
and reject subsequent requests until reboot.

## Block registry and primary MBR partitions

`blkdev_register(backend, type)` retains a physical backend and discovers its
mountable partitions. Backends must remain alive for the registry lifetime.
`blkdev_device_get` enumerates physical devices, including failed probes;
`blkdev_partition_get` enumerates supported views and their parent, start LBA,
and original MBR entry number. `blkdev_partition_io` returns the I/O view and
`blkdev_root` identifies the preferred boot view. Registration is synchronous;
filesystem operations need no additional locks under the kernel lock.

A bare FAT32 boot sector is recognized by its jump and BPB geometry. Otherwise
the sector must contain a signed MBR. FAT32 primary types `0x0b` and `0x0c`,
plus hidden variants `0x1b` and `0x1c`, produce views in table order. Occupied
entries must have valid boot indicators, nonzero starts and lengths, fit the
physical disk, and not overlap, including entries for other filesystems.
Malformed tables produce no partial views. Other primary filesystems are
ignored; protective GPT and extended entries reject the disk as unsupported.
A configured VirtIO disk with no usable boot view fails without ramdisk fallback.
The embedded ramdisk remains registered and mounted alongside external volumes.

To test MBR boot without altering the shared userfs artifact:

```bash
tests/create-mbr-image.py --output build/mbr-root.img build/userfs/aarch64/koraos.img
tests/run-qemu-smoke.py --target qemu_virt --disk build/mbr-root.img
```

The helper accepts up to four bare FAT32 source images, aligns partitions at
2048-sector boundaries, and atomically publishes a separate scratch image.
It preserves source images and never updates the shared producer artifact.
FAT32 remains read-only. Every valid registered FAT32 view joins the namespace;
a failed preferred boot mount never substitutes another mounted volume.

## Volumes, paths and current directory

Device slots are named `df0:`, `df1:`, and so on across all backend types. A
slot represents a mountable partition, including a bare FAT32 image. An
embedded ramdisk normally occupies `df0:`; attached VirtIO partitions follow.
Slots retain their registry indices even if a partition fails to mount.
Volume names come from the FAT label, preferring the root-directory label over
the BPB copy. Matching of device names, volume names and path components is
case-insensitive for ASCII. Unicode filenames remain UTF-8 with exact
non-ASCII matching.

A unique volume label can select that volume, for example `extras:docs/file.txt`.
Repeated labels are ambiguous and must be addressed by device slot. Device
names take precedence over colliding labels. `sys:` and `c:` are reserved for
assigns introduced in the next step.

Each task starts at the boot volume root or inherits its parent's cwd. The
filesystem receives cwd explicitly; a child's directory change does not alter
its parent. Paths resolve as follows:

- `df1:docs/file.txt` and `extras:docs/file.txt` start at the selected root.
- `/docs/file.txt` starts at the root of the current volume.
- `file.txt` and `../file.txt` start at the current directory.
- `.` preserves the current directory; `..` at a volume root stays there.

Components are checked as the path is walked. `missing/..` fails because the
missing directory must exist before its parent can be selected. A filename
followed by `/..` also fails. Failed `chdir` calls preserve cwd. There is no
synthetic `/<volume>/...` tree.

`getcwd` writes a canonical volume-qualified string, for example `extras:docs`
or `extras:` at the root. It uses a unique label when that label resolves back
to the same volume; otherwise it uses the device slot. It returns `0` on
success and `-1` for an invalid or undersized buffer. Canonical paths within
a volume are bounded to 4095 bytes, excluding the NUL terminator. Qualified
input paths allow an additional 32 bytes for a prefix and separator, so the
longest valid `getcwd` result can be passed back to `chdir`. A filename
component retains the existing 765-byte UTF-8 limit.

The file syscalls and explicit program paths use this resolver. Bare program
names continue to search the boot volume's `/bin` until command assigns arrive
in step 8.3. Shell navigation built-ins and the `volumes` listing command arrive
in that step too. The `nsprobe` test program exercises `chdir` and `getcwd` now.

```bash
tests/create-multivolume-image.py --output build/multi-root.img build/userfs/aarch64/koraos.img
tests/run-qemu-smoke.py --target qemu_virt --disk build/multi-root.img --multi-volume
```

This fixture relabels separate copies as `BOOT` and `EXTRAS`, deliberately
leaves their BPB label copies stale, and checks that the original shared userfs
SHA256 remains unchanged.

## Shared producer and dependencies

All current boards share AArch64 userland and the syscall ABI, so one userfs
artifact serves all four kernels and Debug/Release configurations. User headers,
program sources, runtime assembly and added/removed fsroot files trigger updates.
Image generation is atomic. Kernels depend on the prepared image and reassemble
their blob when it changes. `--userfs-only` builds it alone; `--userfs-dir` consumes
an explicitly prepared artifact without rebuilding. A future userland ISA/ABI or
configuration change needs a distinct artifact, rather than sharing incompatible
binaries.
