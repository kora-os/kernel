#!/usr/bin/env python3
"""Wrap up to four bare FAT32 images in an MBR without modifying the inputs."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import tempfile

SECTOR = 512
ALIGNMENT = 2048


def create_image(sources, destination):
    sources = [Path(source) for source in sources]
    destination = Path(destination)
    if not 1 <= len(sources) <= 4:
        raise ValueError("one to four source volumes required")
    if any(source.resolve() == destination.resolve() for source in sources):
        raise ValueError("output must be separate from every source volume")
    layout = []
    next_lba = ALIGNMENT
    for source in sources:
        size = source.stat().st_size
        if not size or size % SECTOR:
            raise ValueError("source must contain whole 512-byte sectors")
        with source.open("rb") as stream:
            boot = stream.read(SECTOR)
        if (boot[510:512] != b"\x55\xaa" or boot[11:13] != b"\x00\x02"
                or boot[17:19] != b"\x00\x00" or boot[22:24] != b"\x00\x00"
                or not int.from_bytes(boot[36:40], "little")):
            raise ValueError("source must be a bare FAT32 image")
        sectors = size // SECTOR
        start = (next_lba + ALIGNMENT - 1) // ALIGNMENT * ALIGNMENT
        if start + sectors > 0xffffffff:
            raise ValueError("image exceeds the 32-bit block sector API")
        layout.append((source, start, sectors))
        next_lba = start + sectors
    mbr = bytearray(SECTOR)
    mbr[510:512] = b"\x55\xaa"
    for index, (_, start, count) in enumerate(layout):
        # LBA-addressed FAT32. Saturated CHS values avoid claiming geometry.
        struct.pack_into("<B3sB3sII", mbr, 446 + index * 16,
                         0, b"\xfe\xff\xff", 0x0c, b"\xfe\xff\xff", start, count)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=destination.name + ".", dir=destination.parent)
    try:
        with os.fdopen(fd, "wb") as output:
            output.truncate(next_lba * SECTOR)
            output.write(mbr)
            for source, start, _ in layout:
                output.seek(start * SECTOR)
                with source.open("rb") as stream:
                    shutil.copyfileobj(stream, output)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    return layout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("volumes", nargs="+", help="bare FAT32 image(s), preserved unchanged")
    parser.add_argument("--output", required=True, help="separate scratch MBR disk image")
    args = parser.parse_args()
    try:
        layout = create_image(args.volumes, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    for index, (source, start, count) in enumerate(layout, 1):
        print("primary %d: %s at LBA %d, %d sectors" % (index, source, start, count))


if __name__ == "__main__":
    main()
