#!/usr/bin/env python3
"""Disposable FAT32 namespace images with independent VFAT and host-tool checks."""
from pathlib import Path
import argparse
import hashlib
import struct
import subprocess
import tempfile


def run(*args):
    result = subprocess.run(args, capture_output=True)
    if result.returncode:
        print(result.stdout.decode("utf-8", errors="replace"))
        print(result.stderr.decode("utf-8", errors="replace"))
        result.check_returncode()
    return result


def create(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for previous in directory.glob("*.img"):
        previous.unlink()
    image = directory / "source.img"
    with image.open("wb") as stream:
        stream.truncate(64 * 1024 * 1024)
    run("mformat", "-i", str(image), "-F", "-c", "1", "-v", "NAMESPACE", "::")
    run("mmd", "-i", str(image), "::/bin", "::/a", "::/b", "::/emptydir", "::/a/subdir")
    with tempfile.TemporaryDirectory(prefix="kora-ns-source-") as scratch:
        payload = Path(scratch) / "payload"
        for destination, contents in (("::/a/victim.bin", b"V" * 1300),
                                      ("::/a/source.bin", b"source content"),
                                      ("::/a/owned.txt", b"owned original")):
            payload.write_bytes(contents)
            run("mcopy", "-i", str(image), str(payload), destination)
    # A valid external VFAT entry has a long name resembling a generated
    # short alias but a distinct raw 8.3 alias. New aliases must reserve both.
    raw = bytearray(image.read_bytes())
    reserved = struct.unpack_from("<H", raw, 14)[0]
    fatsz = struct.unpack_from("<I", raw, 36)[0]
    data = reserved + raw[16] * fatsz
    root = struct.unpack_from("<I", raw, 44)[0]
    root_start = (data + root - 2) * 512
    for offset in range(root_start, root_start + 512, 32):
        if raw[offset:offset + 11] == b"A          ":
            cluster = struct.unpack_from("<H", raw, offset + 26)[0]
            break
    else:
        raise RuntimeError("source A directory missing")
    start = (data + cluster - 2) * 512
    for offset in range(start, start + 512, 32):
        if raw[offset:offset + 11] == b"OWNED   TXT":
            short = raw[offset:offset + 32]
            raw[offset + 32:start + 512] = raw[offset:start + 480]
            entry = bytearray([255] * 32)
            entry[0], entry[11], entry[12] = 0x41, 0x0f, 0
            checksum = 0
            for value in short[:11]:
                checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 255
            entry[13] = checksum
            entry[26:28] = bytes(2)
            units = "COLLIS~1.TXT".encode("utf-16-le") + bytes(2)
            units += bytes([255, 255]) * (13 - len(units) // 2)
            entry[1:11], entry[14:26], entry[28:32] = units[:10], units[10:22], units[22:26]
            raw[offset:offset + 32] = entry
            break
    else:
        raise RuntimeError("source owned short entry missing")
    image.write_bytes(raw)
    run("fsck.fat", "-n", str(image))
    run("mtype", "-i", str(image), "::/a/COLLIS~1.TXT")
    (directory / "source.sha256").write_text(hashlib.sha256(image.read_bytes()).hexdigest())


class Disk:
    def __init__(self, path):
        self.path = path
        self.raw = path.read_bytes()
        self.reserved = struct.unpack_from("<H", self.raw, 14)[0]
        self.fatsz = struct.unpack_from("<I", self.raw, 36)[0]
        self.spc = self.raw[13]
        self.data = self.reserved + self.raw[16] * self.fatsz
        self.root = struct.unpack_from("<I", self.raw, 44)[0]
        self.entries = {}
        self.walk(self.root, "", "", 0)

    def chain(self, first):
        seen = set()
        while first < 0x0ffffff8:
            assert first >= 2 and first not in seen, "invalid directory chain"
            seen.add(first)
            start = (self.data + (first - 2) * self.spc) * 512
            yield self.raw[start:start + self.spc * 512]
            first = struct.unpack_from("<I", self.raw, self.reserved * 512 + first * 4)[0] & 0x0fffffff

    def walk(self, cluster, prefix, aliases, parent):
        long_entries = []
        dots = {}
        for sector in self.chain(cluster):
            for offset in range(0, len(sector), 32):
                entry = sector[offset:offset + 32]
                if entry[0] == 0:
                    if prefix:
                        assert dots == {".": cluster, "..": parent}, f"bad dot links for {prefix}: {dots}"
                    return
                if entry[0] == 0xe5:
                    long_entries.clear()
                    continue
                if entry[11] == 0x0f:
                    long_entries.append(entry)
                    continue
                if entry[11] & 8:
                    long_entries.clear()
                    continue
                short = entry[:11]
                alias = short[:8].decode("ascii").rstrip()
                extension = short[8:].decode("ascii").rstrip()
                if extension:
                    alias += "." + extension
                name = alias.lower()
                if long_entries:
                    checksum = 0
                    for value in short:
                        checksum = (((checksum & 1) << 7) + (checksum >> 1) + value) & 255
                    count = long_entries[0][0] & 31
                    assert long_entries[0][0] & 64 and len(long_entries) == count
                    pieces = {}
                    for index, item in enumerate(long_entries):
                        assert item[0] & 31 == count - index and item[13] == checksum
                        assert item[12] == 0 and item[26:28] == b"\0\0"
                        pieces[item[0] & 31] = item[1:11] + item[14:26] + item[28:32]
                    units = b"".join(pieces[index] for index in range(1, count + 1))
                    words = [units[i:i + 2] for i in range(0, len(units), 2)]
                    if b"\0\0" in words:
                        end = words.index(b"\0\0")
                        assert all(word == b"\xff\xff" for word in words[end + 1:])
                        words = words[:end]
                    name = b"".join(words).decode("utf-16-le", errors="strict")
                    assert len(b"".join(words)) <= 510
                long_entries.clear()
                first = struct.unpack_from("<H", entry, 20)[0] << 16 | struct.unpack_from("<H", entry, 26)[0]
                if alias in (".", ".."):
                    dots[alias] = first
                    continue
                full = prefix + "/" + name
                assert full not in self.entries, f"duplicate name: {full}"
                self.entries[full] = (aliases + "/" + alias, struct.unpack_from("<I", entry, 28)[0], first, entry[11])
                if entry[11] & 16:
                    self.walk(first, full, aliases + "/" + alias, 0 if cluster == self.root else cluster)
        if prefix:
            assert dots == {".": cluster, "..": parent}

    def readback(self, names):
        with tempfile.TemporaryDirectory(prefix="kora-ns-readback-") as scratch:
            for path, contents in names.items():
                folded = "".join(char.lower() if ord(char) < 128 else char for char in path)
                matches = [value for name, value in self.entries.items()
                           if "".join(char.lower() if ord(char) < 128 else char for char in name) == folded]
                assert len(matches) == 1, f"missing or ambiguous {path} in {self.path.name}"
                alias, size, _, attributes = matches[0]
                assert not attributes & 16 and size == len(contents)
                target = Path(scratch) / "payload"
                if target.exists():
                    target.unlink()
                run("mcopy", "-i", str(self.path), "::" + alias, str(target))
                assert target.read_bytes() == contents, f"wrong {path} bytes"


def verify(directory):
    assert hashlib.sha256((directory / "source.img").read_bytes()).hexdigest() == (directory / "source.sha256").read_text()
    images = sorted(path for path in directory.glob("*.img") if path.name != "source.img")
    assert len(images) == 83, "missing namespace normal or fault mutation exports"
    for image in images:
        result = run("fsck.fat", "-n", str(image))
        disk = Disk(image)
        fat0 = disk.raw[disk.reserved * 512:(disk.reserved + disk.fatsz) * 512]
        fat1 = disk.raw[(disk.reserved + disk.fatsz) * 512:(disk.reserved + 2 * disk.fatsz) * 512]
        assert fat0 == fat1, f"divergent FAT mirrors: {image.name}"
        if image.name == "normal.img":
            expected = {"/a/plain.txt": b"plain", "/a/COLLIS~1.TXT": b"owned original",
                        "/a/collision filename.txt": b"new alias", "/a/Δοκιμή.txt": b"greek",
                        "/a/astral-😀.txt": b"astral", "/a/" + "中" * 255: b"max",
                        "/a/" + "😀" * 127 + "a": b"max astral",
                        "/b/moved.bin": b"source content", "/a/case.txt": b"case",
                        "/a/reused slots long filename.txt": b"reuse"}
            for i in range(12):
                expected[f"/a/collision filename number {i}.txt"] = b"alias"
            for i in range(40):
                expected[f"/b/entry-{i:02}.txt"] = b"entry"
            disk.readback(expected)
            assert "/a/victim.bin" not in disk.entries and "/a/source.bin" not in disk.entries
            assert "/b/moved directory" in disk.entries and "/a/subdir" not in disk.entries
            assert "/emptydir" not in disk.entries
        else:
            operation = image.name.split("-")[1]
            expected = {"/a/victim.bin": b"V" * 1300, "/a/source.bin": b"source content"}
            if operation == "create": expected["/b/fault created xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.txt"] = b"created"
            elif operation == "mkdir": assert "/b/fault directory xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" in disk.entries
            elif operation == "unlink": del expected["/a/victim.bin"]; assert "/a/victim.bin" not in disk.entries
            elif operation == "rmdir": assert "/emptydir" not in disk.entries
            elif operation == "rename": del expected["/a/source.bin"]; expected["/b/fault renamed xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.bin"] = b"source content"
            elif operation == "dirrename": assert "/a/subdir" not in disk.entries and "/b/fault moved directory xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" in disk.entries
            expected["/a/COLLIS~1.TXT"] = b"owned original"
            disk.readback(expected)
    print(f"FAT32 namespace: {len(images)} fsck-clean images, exact alias readback, raw VFAT/dot links and immutable source verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("create", "verify"))
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    (create if args.mode == "create" else verify)(args.directory)
