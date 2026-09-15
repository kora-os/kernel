KoraOS filesystem online.

This file lives on a FAT32 volume that is built on the host by
create-fs-image.sh and embedded into the kernel image as a read-only
ramdisk. The kernel reads it back through the block device -> FAT32 stack.
