#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Real-tool regression checks for shared userfs invalidation and atomic output."""

import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest
import os

ROOT = Path(__file__).resolve().parent.parent


class UserFSTests(unittest.TestCase):
    def run_tool(self, *args, env=None, ok=True):
        result = subprocess.run(
            args, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT
        )
        if ok:
            self.assertEqual(result.returncode, 0, result.stdout)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def test_incremental_contents_and_atomic_failure(self):
        with tempfile.TemporaryDirectory(prefix="kora userfs ") as temporary:
            base = Path(temporary).resolve()
            source = base / "source"
            build = base / "userfs"
            source.mkdir()
            for directory in ("cmake", "user", "fsroot", "tests/user"):
                shutil.copytree(ROOT / directory, source / directory)
            shutil.copy2(ROOT / "create-fs-image.sh", source / "create-fs-image.sh")
            self.run_tool(
                "cmake",
                "-S",
                str(source / "cmake/userfs"),
                "-B",
                str(build),
                "-DCMAKE_BUILD_TYPE=Debug",
            )

            def rebuild():
                self.run_tool("cmake", "--build", str(build), "--parallel", "2")

            def snapshot():
                return {
                    p.name: p.stat().st_mtime_ns for p in (build / "user").glob("*.elf")
                }

            def listing():
                return self.run_tool(
                    "mdir", "-b", "-i", str(build / "koraos.img"), "::/bin/"
                ).stdout

            rebuild()
            image = build / "koraos.img"
            initial = snapshot()
            initial_image = image.stat().st_mtime_ns
            rebuild()
            self.assertEqual(snapshot(), initial)
            self.assertEqual(image.stat().st_mtime_ns, initial_image)
            self.assertIn("/cat", listing())

            # Removing a registration must remove its image entry even if the
            # old ELF remains in the producer's output directory.
            time.sleep(1.1)
            module = source / "cmake/userfs.cmake"
            original = module.read_text()
            self.assertIn("ls cat)", original)
            module.write_text(original.replace("ls cat)", "ls)"))
            rebuild()
            self.assertNotIn("/cat", listing())
            self.assertTrue((build / "user/cat.elf").exists())

            # Header and shipped-file removals must invalidate their outputs,
            # including when all remaining inputs predate the current image.
            time.sleep(1.1)
            header = source / "user/libk/regression.h"
            header.write_text("/* dependency fixture */\n")
            before = snapshot()
            rebuild()
            self.assertNotEqual(snapshot()["hello.elf"], before["hello.elf"])
            time.sleep(1.1)
            header.unlink()
            before = snapshot()
            rebuild()
            self.assertNotEqual(snapshot()["hello.elf"], before["hello.elf"])
            time.sleep(1.1)
            data = source / "fsroot/regression.txt"
            data.write_text("fixture")
            before = snapshot()
            rebuild()
            self.assertEqual(snapshot(), before)
            self.assertEqual(
                self.run_tool("mtype", "-i", str(image), "::/regression.txt").stdout,
                "fixture",
            )
            time.sleep(1.1)
            data.unlink()
            before = snapshot()
            before_image = image.stat().st_mtime_ns
            rebuild()
            self.assertEqual(snapshot(), before)
            self.assertNotEqual(image.stat().st_mtime_ns, before_image)
            self.run_tool("mtype", "-i", str(image), "::/regression.txt", ok=False)

            # Changes to the shared compile flags must rebuild actual ELFs.
            time.sleep(1.1)
            old_elf = (build / "user/hello.elf").read_bytes()
            module.write_text(
                module.read_text().replace("-fPIE -O2 -Wall", "-fPIE -O0 -Wall")
            )
            rebuild()
            self.assertNotEqual((build / "user/hello.elf").read_bytes(), old_elf)
            before = snapshot()
            before_image = image.stat().st_mtime_ns
            self.run_tool(
                "cmake",
                "-S",
                str(source / "cmake/userfs"),
                "-B",
                str(build),
                "-DCMAKE_BUILD_TYPE=Release",
            )
            rebuild()
            self.assertEqual(snapshot(), before)
            self.assertEqual(image.stat().st_mtime_ns, before_image)

            # A failed population never replaces a previously valid image.
            digest = hashlib.sha256(image.read_bytes()).digest()
            tools = base / "tools"
            tools.mkdir()
            fake = tools / "mcopy"
            fake.write_text("#!/bin/sh\nexit 43\n")
            fake.chmod(0o755)
            env = dict(os.environ, PATH=str(tools) + os.pathsep + os.environ["PATH"])
            self.run_tool(
                str(source / "create-fs-image.sh"),
                "--output",
                str(image),
                "--fsroot",
                str(source / "fsroot"),
                "--programs-file",
                str(build / "userfs-programs.txt"),
                env=env,
                ok=False,
            )
            self.assertEqual(hashlib.sha256(image.read_bytes()).digest(), digest)
            self.assertFalse(list(build.glob(".koraos-fs-image.*")))


if __name__ == "__main__":
    unittest.main(verbosity=2)
