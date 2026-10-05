#!/usr/bin/env python3
"""Check scratch image source protection, exact registration and failed publication."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("write_image", Path(__file__).with_name("create-write-image.py"))
write_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(write_image)


class ScratchImageTests(unittest.TestCase):
    def test_registered_inputs_and_aliases(self):
        with tempfile.TemporaryDirectory(prefix="kora-write-image-tests-") as temporary:
            root = Path(temporary)
            inputs = root / "prepared"
            (inputs / "user").mkdir(parents=True)
            shared = inputs / "koraos.img"
            with shared.open("wb") as stream:
                stream.truncate(2 * 1024 * 1024)
            subprocess.run(["mformat", "-F", "-i", str(shared), "::"], check=True)
            subprocess.run(["mmd", "-i", str(shared), "::/bin"], check=True)
            registered = inputs / "user/registered.elf"
            registered.write_bytes(b"prepared registered ELF bytes\n")
            stale = inputs / "user/stale.elf"
            stale.write_bytes(b"unregistered stale ELF bytes\n")
            subprocess.run(["mcopy", "-i", str(shared), str(registered), "::/bin/registered"], check=True)
            source_hashes = {path: write_image.digest(path) for path in (shared, registered, stale)}
            output_alias = root / "elf-alias.img"
            output_alias.symlink_to(registered)
            directory_alias = root / "prepared-alias"
            directory_alias.symlink_to(inputs, target_is_directory=True)
            fsroot_alias = root / "fsroot-alias.img"
            fsroot_alias.symlink_to(write_image.ROOT / "fsroot/README.TXT")
            fsroot_hash = write_image.digest(write_image.ROOT / "fsroot/README.TXT")
            for output in (shared, registered, inputs / "new.img", output_alias,
                           directory_alias / "user/registered.elf", fsroot_alias):
                with self.assertRaises(ValueError):
                    write_image.create_image(inputs, output)
                self.assertEqual({path: write_image.digest(path) for path in source_hashes}, source_hashes)
                self.assertEqual(write_image.digest(write_image.ROOT / "fsroot/README.TXT"), fsroot_hash)
            self.assertTrue(output_alias.is_symlink())
            external = root / "external registered.elf"
            external.write_bytes(registered.read_bytes())
            registered.unlink()
            registered.symlink_to(external)
            with self.assertRaises(ValueError):
                write_image.create_image(inputs, external)
            self.assertEqual(write_image.digest(external), source_hashes[registered])
            self.assertEqual({path: write_image.digest(path) for path in source_hashes}, source_hashes)
            outside_shared = root / "external shared.img"
            shared.rename(outside_shared)
            shared.symlink_to(outside_shared)
            with self.assertRaises(ValueError):
                write_image.create_image(inputs, outside_shared)
            self.assertEqual(write_image.digest(outside_shared), source_hashes[shared])
            destination = root / "scratch.img"
            write_image.create_image(inputs, destination)
            listing = subprocess.check_output(["mdir", "-b", "-i", str(destination), "::/bin"], text=True)
            self.assertIn("/registered", listing)
            self.assertNotIn("/stale", listing)
            self.assertEqual(destination.stat().st_size, 64 * 1024 * 1024)
            exported = root / "readback"
            subprocess.run(["mcopy", "-i", str(destination), "::/bin/registered", str(exported)], check=True)
            self.assertEqual(exported.read_bytes(), registered.read_bytes())
            self.assertEqual({path: write_image.digest(path) for path in source_hashes}, source_hashes)
            output_hash = write_image.digest(destination)
            registered.write_bytes(b"mismatched prepared ELF\n")
            with self.assertRaises(ValueError):
                write_image.create_image(inputs, destination)
            self.assertEqual(write_image.digest(destination), output_hash)
            self.assertEqual(write_image.digest(shared), source_hashes[shared])


if __name__ == "__main__":
    unittest.main()
