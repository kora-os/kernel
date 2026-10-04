#!/usr/bin/env python3
"""Check scratch MBR generation, layout, and preservation of source artifacts."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("mbr_image", Path(__file__).with_name("create-mbr-image.py"))
mbr_image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mbr_image)


class MbrImageTests(unittest.TestCase):
    def test_layout_and_preservation(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "volume.img"
            image = bytearray(4096)
            image[11:13] = b"\x00\x02"
            image[36] = 1
            image[510:512] = b"\x55\xaa"
            image[512:] = b"x" * (len(image) - 512)
            source.write_bytes(image)
            destination = root / "scratch.img"
            layout = mbr_image.create_image([source] * 4, destination)
            self.assertEqual(source.read_bytes(), image)
            result = destination.read_bytes()
            self.assertEqual(result[510:512], b"\x55\xaa")
            self.assertEqual(len(result), (layout[-1][1] + 8) * 512)
            for index, (_, start, count) in enumerate(layout):
                entry = struct.unpack_from("<B3sB3sII", result, 446 + 16 * index)
                self.assertEqual(entry[2:], (0x0c, b"\xfe\xff\xff", start, 8))
                self.assertEqual(start % 2048, 0)
                self.assertEqual(count, 8)
                self.assertEqual(result[start * 512:(start + count) * 512], image)
            for invalid_sources, output in [([], destination), ([source] * 5, destination),
                                            ([source], source)]:
                with self.assertRaises(ValueError):
                    mbr_image.create_image(invalid_sources, output)
                self.assertEqual(source.read_bytes(), image)
                self.assertEqual(destination.read_bytes(), result)
            source.write_bytes(b"bad")
            with self.assertRaises(ValueError):
                mbr_image.create_image([source], destination)
            self.assertEqual(destination.read_bytes(), result)
            source.write_bytes(bytes(512))
            with self.assertRaises(ValueError):
                mbr_image.create_image([source], destination)
            self.assertEqual(destination.read_bytes(), result)


if __name__ == "__main__":
    unittest.main()
