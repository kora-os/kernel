#!/usr/bin/env python3
"""Create disposable FAT32 write fixtures and verify exported images with host tools."""
from pathlib import Path
import argparse
import hashlib
import struct
import subprocess
import tempfile


def pattern(length):
    return bytes((i * 37 + 11) % 256 for i in range(length))


def run(*args):
    subprocess.run(args, check=True)


def check_fsck(path):
    run("fsck.fat", "-n", str(path))


def create(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for previous in directory.glob("fault-*.img"):
        previous.unlink()
    image = directory / "source.img"
    with image.open("wb") as stream:
        stream.truncate(64 * 1024 * 1024)
    run("mformat", "-i", str(image), "-F", "-c", "1", "-v", "WRITE", "::")
    with tempfile.TemporaryDirectory(prefix="kora-write-") as scratch:
        scratch = Path(scratch)
        for name, length in (("original.bin", 700), ("empty.bin", 0),
                             ("fragment.bin", 512), ("blocker.bin", 512),
                             ("pressure.bin", 16384), ("reuse.bin", 0)):
            source = scratch / name
            source.write_bytes(pattern(length))
            run("mcopy", "-i", str(image), str(source), "::/" + name)
        extra = scratch / "fragment.bin"
        extra.write_bytes(pattern(3000))
        run("mcopy", "-o", "-i", str(image), str(extra), "::/fragment.bin")
    # Relocate the second fragment cluster to a distant free cluster. This
    # constructs a valid fragmented chain independent of mtools' overwrite
    # allocation strategy; the number of free clusters stays unchanged.
    raw = bytearray(image.read_bytes())
    reserved = struct.unpack_from("<H", raw, 14)[0]
    fatsz = struct.unpack_from("<I", raw, 36)[0]
    data = reserved + raw[16] * fatsz
    root = struct.unpack_from("<I", raw, 44)[0]
    start = (data + root - 2) * 512
    for offset in range(start, start + 512, 32):
        if raw[offset:offset + 11] == b"FRAGMENTBIN":
            first = (struct.unpack_from("<H", raw, offset + 20)[0] << 16
                     | struct.unpack_from("<H", raw, offset + 26)[0])
            old = struct.unpack_from("<I", raw, reserved * 512 + first * 4)[0] & 0x0fffffff
            next_cluster = struct.unpack_from("<I", raw, reserved * 512 + old * 4)[0]
            relocated = old + 100
            assert struct.unpack_from("<I", raw, reserved * 512 + relocated * 4)[0] == 0
            old_data = (data + old - 2) * 512
            new_data = (data + relocated - 2) * 512
            raw[new_data:new_data + 512] = raw[old_data:old_data + 512]
            for fat in range(raw[16]):
                base = (reserved + fat * fatsz) * 512
                struct.pack_into("<I", raw, base + first * 4, relocated)
                struct.pack_into("<I", raw, base + relocated * 4, next_cluster)
                struct.pack_into("<I", raw, base + old * 4, 0)
            break
    else:
        raise RuntimeError("fragmented-file short entry missing")
    image.write_bytes(raw)
    check_fsck(image)
    (directory / "source.sha256").write_text(hashlib.sha256(image.read_bytes()).hexdigest())


def verify(directory):
    source = directory / "source.img"
    assert hashlib.sha256(source.read_bytes()).hexdigest() == (directory / "source.sha256").read_text(), "immutable fixture changed"
    expected = {
        "original.bin": pattern(17) + b"partial-sector" + pattern(700)[31:] + b"A" * 1400,
        "empty.bin": b"E" * 1700,
        "fragment.bin": pattern(3000)[:777],
        "blocker.bin": pattern(512),
        "pressure.bin": b"P" * 16384,
        "reuse.bin": b"R" * 2200,
    }
    with tempfile.TemporaryDirectory(prefix="kora-readback-") as scratch:
        for image_name in ("written.img", "unknown-info.img", "stale-info.img"):
            image = directory / image_name
            check_fsck(image)
            for name, content in expected.items():
                target = Path(scratch) / name
                if target.exists():
                    target.unlink()
                run("mcopy", "-i", str(image), "::/" + name, str(target))
                assert target.read_bytes() == content, f"{image_name}: wrong content for {name}"
            raw = image.read_bytes()
            reserved = struct.unpack_from("<H", raw, 14)[0]
            fatsz = struct.unpack_from("<I", raw, 36)[0]
            fat0 = raw[reserved * 512:(reserved + fatsz) * 512]
            fat1 = raw[(reserved + fatsz) * 512:(reserved + 2 * fatsz) * 512]
            assert fat0 == fat1, f"{image_name}: mirrored FATs diverge"
        fault_images = list(directory.glob("fault-*.img"))
        assert len(fault_images) == 24, "missing interrupted-operation exports"
        for image in sorted(fault_images):
            check_fsck(image)
            growing = image.name.startswith("fault-grow-")
            for name, content in (("original.bin", pattern(700) + b"G" * 600 if growing else pattern(700)),
                                  ("pressure.bin", pattern(16384) if growing else pattern(777)),
                                  ("fragment.bin", pattern(3000)),
                                  ("blocker.bin", pattern(512))):
                target = Path(scratch) / name
                if target.exists():
                    target.unlink()
                run("mcopy", "-i", str(image), "::/" + name, str(target))
                assert target.read_bytes() == content, f"{image.name}: wrong recovery content for {name}"
    print("FAT32 writes: 27 fsck-clean images, mirrors, mtools readback and immutable source verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("create", "verify"))
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    (create if args.mode == "create" else verify)(args.directory)
