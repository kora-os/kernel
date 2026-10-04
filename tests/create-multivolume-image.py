#!/usr/bin/env python3
"""Build a two-partition scratch root from copies of the immutable shared userfs."""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

spec = importlib.util.spec_from_file_location("mbr_image", Path(__file__).with_name("create-mbr-image.py"))
mbr_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mbr_image)


def create_image(source, output):
    source, output = Path(source), Path(output)
    if source.resolve() == output.resolve():
        raise ValueError("scratch output must be separate from shared userfs")
    before = hashlib.sha256(source.read_bytes()).digest()
    with tempfile.TemporaryDirectory(prefix="kora-volume-fixture-") as temporary:
        volumes = []
        for label in ("BOOT", "EXTRAS"):
            volume = Path(temporary) / (label.lower() + ".img")
            shutil.copyfile(source, volume)
            subprocess.run(["mlabel", "-i", str(volume), "::" + label], check=True)
            # Force a stale BPB copy: root-directory labels must take precedence.
            with volume.open("r+b") as stream:
                stream.seek(71)
                stream.write(b"STALE      ")
            if label == "EXTRAS":
                marker = Path(temporary) / "extras-marker.txt"
                marker.write_text("EXTRAS volume marker: this content differs from the boot disk.\n")
                subprocess.run(["mcopy", "-o", "-i", str(volume), str(marker), "::/README.TXT"],
                               check=True)
            volumes.append(volume)
        mbr_image.create_image(volumes, output)
    if hashlib.sha256(source.read_bytes()).digest() != before:
        raise RuntimeError("shared userfs changed during scratch fixture generation")
    print("created scratch BOOT and EXTRAS partitions; shared userfs SHA256 preserved")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source")
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        create_image(args.source, args.output)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
