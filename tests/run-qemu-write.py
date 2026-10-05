#!/usr/bin/env python3
"""QEMU writable-root integration with fsck, exact host readback and source hashes."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("write_image", ROOT / "tests/create-write-image.py")
write_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(write_image)


def verify_image(image, output):
    fsck = subprocess.run(["fsck.fat", "-n", str(image)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (output / "fsck.log").write_text(fsck.stdout)
    if fsck.returncode:
        raise RuntimeError("post-QEMU fsck failed:\n" + fsck.stdout)
    pattern = bytearray((index * 37 + 11) % 256 for index in range(8193))
    pattern[509:528] = b"Z" * 19
    expected = {
        "/write tests/final data.bin": bytes(pattern) + b"append A\nappend B\n",
        "/write tests/truncate.txt": b"short\n",
        "/write tests/résumé-notes 東京.txt": b"unicode payload\n",
        "/write tests/nested new/check.txt": b"nested\n",
        "/write tests/read only create.txt": b"",
        "/quoted copy.txt": (ROOT / "fsroot/README.TXT").read_bytes(),
        "/README.TXT": (ROOT / "fsroot/README.TXT").read_bytes(),
    }
    with tempfile.TemporaryDirectory(prefix="kora-write-readback-") as temporary:
        for index, (name, content) in enumerate(expected.items()):
            target = Path(temporary) / str(index)
            subprocess.run(["mcopy", "-i", str(image), "::" + name, str(target)], check=True)
            if target.read_bytes() != content:
                raise RuntimeError("wrong persistent content: " + name)
    absent = ("/write tests/initial data.bin", "/write tests/delete me.txt",
              "/write tests/remove dir", "/write tests/nested old", "/write tests/pinned parent",
              "/write tests/no fd create.txt", "/moved parent", "/renamed tests", "/write cli")
    for name in absent:
        result = subprocess.run(["mdir", "-b", "-i", str(image), "::" + name],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode == 0:
            raise RuntimeError("removed/rejected path still exists: " + name)
    raw = image.read_bytes()
    reserved = struct.unpack_from("<H", raw, 14)[0]
    fat_count = raw[16]
    fat_sectors = struct.unpack_from("<I", raw, 36)[0]
    primary = raw[reserved * 512:(reserved + fat_sectors) * 512]
    for index in range(1, fat_count):
        start = (reserved + index * fat_sectors) * 512
        if raw[start:start + len(primary)] != primary:
            raise RuntimeError("FAT mirrors differ after sync")
    flags = struct.unpack_from("<I", primary, 4)[0]
    if flags & 0x0c000000 != 0x0c000000:
        raise RuntimeError("volume remains dirty or has a recorded I/O error")
    (output / "verification.json").write_text(json.dumps({
        "files": {name: len(content) for name, content in expected.items()},
        "absent": absent, "fat_mirrors": fat_count, "clean": True,
    }, indent=2) + "\n")
    print("QEMU writes: fsck clean, FAT mirrors/flags, exact mtools contents and removed paths verified")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--userfs-dir", default=str(ROOT / "build/userfs/aarch64"))
    parser.add_argument("--build-dir", default=str(ROOT / "build"))
    parser.add_argument("--release", action="store_true")
    parser.add_argument("--out")
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    args = parser.parse_args()
    output = Path(args.out or Path(args.build_dir) / ("release" if args.release else "debug") /
                  "qemu_virt/qemu-write").resolve()
    write_image.validate_output(args.userfs_dir, output)
    image = output / "scratch.img"
    write_image.validate_output(args.userfs_dir, image)
    output.mkdir(parents=True, exist_ok=True)
    shared = Path(args.userfs_dir).resolve() / "koraos.img"
    before = write_image.digest(shared)
    try:
        write_image.create_image(args.userfs_dir, image)
        subprocess.run(["fsck.fat", "-n", str(image)], check=True)
        command = [str(ROOT / "tests/run-qemu-smoke.py"), "--target", "qemu_virt",
                   "--build-dir", str(args.build_dir), "--disk", str(image), "--disk-writable",
                   "--write-test", "--repeat", "64", "--qemu", args.qemu, "--out", str(output)]
        if args.release:
            command.append("--release")
        subprocess.run(command, check=True)
        verify_image(image, output)
    finally:
        after = write_image.digest(shared)
        (output / "source.sha256").write_text(before + "\n")
        if after != before:
            raise RuntimeError("immutable shared userfs changed")
        print("immutable shared userfs SHA256 preserved: " + before)


if __name__ == "__main__":
    main()
