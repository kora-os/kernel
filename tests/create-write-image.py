#!/usr/bin/env python3
"""Create a fresh 64 MiB FAT32 scratch root from registered prepared programs,
and the fonts of the shared image."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    hasher = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def validate_output(userfs, output, registered=()):
    userfs, output = Path(userfs).resolve(), Path(output).resolve()
    fsroot = (ROOT / "fsroot").resolve()
    if any(output == protected or protected in output.parents for protected in (userfs, fsroot)):
        raise ValueError("scratch output must be outside prepared userfs and fsroot inputs")
    inputs = [userfs / "koraos.img", *registered,
              *(path for path in fsroot.rglob("*") if path.is_file())]
    for source in inputs:
        if output == Path(source).resolve():
            raise ValueError("scratch output aliases an input file")
    return output


def create_image(userfs, output):
    userfs, output = Path(userfs).resolve(), Path(output).resolve()
    shared = userfs / "koraos.img"
    output = validate_output(userfs, output)
    before = digest(shared)
    names = subprocess.check_output(["mdir", "-b", "-i", str(shared), "::/bin"], text=True).splitlines()
    registered = []
    with tempfile.TemporaryDirectory(prefix="kora-write-root-") as temporary:
        temporary = Path(temporary)
        for path in names:
            if path.endswith("/"):
                continue
            name = path.rsplit("/", 1)[-1]
            if not name or name in (".", ".."):
                raise ValueError("invalid program registration in shared image")
            elf = userfs / "user" / (name + ".elf")
            if not elf.is_file():
                raise ValueError("missing registered prepared ELF: " + name)
            extracted = temporary / name
            subprocess.run(["mcopy", "-i", str(shared), path, str(extracted)], check=True)
            if digest(extracted) != digest(elf):
                raise ValueError("prepared ELF differs from registered shared image: " + name)
            registered.append(elf)
        if not registered:
            raise ValueError("shared image has no registered programs")
        validate_output(userfs, output, registered)
        manifest = temporary / "programs.txt"
        manifest.write_text("".join(str(elf) + "\n" for elf in registered))
        # The fonts (sys:fonts), the test files (sys:tests) and KoraCLI's
        # (sys:koracli), as the shared image has them.
        # (Apart from the programs extracted above: a directory may share a
        # program's name, as koracli does.)
        def copy_directory(directory):
            target = temporary / "directories" / directory
            target.mkdir(parents=True)
            listed = subprocess.run(["mdir", "-b", "-i", str(shared), "::/" + directory], text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            files = []
            for path in listed.stdout.splitlines() if listed.returncode == 0 else []:
                name = path.rsplit("/", 1)[-1]
                if path.endswith("/") or not name or name in (".", ".."):
                    continue
                subprocess.run(["mcopy", "-i", str(shared), path, str(target / name)], check=True)
                files.append(target / name)
            listing = temporary / "directories" / (directory + ".txt")
            listing.write_text("".join(str(f) + "\n" for f in files))
            return files, listing

        fonts, fonts_manifest = copy_directory("fonts")
        _, tests_manifest = copy_directory("tests")
        _, koracli_manifest = copy_directory("koracli")
        subprocess.run([str(ROOT / "create-fs-image.sh"), "--size", "64", "--volume", "WRITE",
                        "--fsroot", str(ROOT / "fsroot"), "--programs-file", str(manifest),
                        "--fonts-file", str(fonts_manifest), "--tests-file", str(tests_manifest),
                        "--koracli-file", str(koracli_manifest),
                        "--output", str(output)], check=True)
    if digest(shared) != before:
        raise RuntimeError("immutable shared userfs changed during scratch generation")
    print("scratch root contains %d registered programs and %d fonts; shared SHA256 %s"
          % (len(registered), len(fonts), before))
    return before


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--userfs-dir", default=str(ROOT / "build/userfs/aarch64"))
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        create_image(args.userfs_dir, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
