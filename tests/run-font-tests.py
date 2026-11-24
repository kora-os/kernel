#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of scripts/bdf2psf.py, the BDF to PSF2 converter the build runs on
the vendored fonts (third_party/spleen): a small BDF made here (a glyph's
box placed by its offsets and clipped, a glyph without a code point left
out, two bytes a row, the Unicode table in UTF-8), BDFs it refuses, and each
Spleen font: its size, every glyph converted bit for bit, the output the
same twice."""

import os
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bdf2psf  # noqa: E402

failures = 0
checks = 0


def check(ok, what):
    global failures, checks
    checks += 1
    if not ok:
        failures += 1
        print("FAIL run-font-tests: %s" % what)


def read_psf(data):
    """(header fields, glyph bitmaps, table entries)."""
    fields = struct.unpack("<8I", data[:32])
    magic, version, header, flags, count, size, height, width = fields
    glyphs = [data[header + i * size:header + (i + 1) * size] for i in range(count)]
    table = data[header + count * size:].split(b"\xff")
    return fields, glyphs, table[:-1] if table[-1] == b"" else table


SMALL = """STARTFONT 2.1
FONTBOUNDINGBOX 10 4 0 -1
CHARS 3
STARTCHAR box
ENCODING 9472
BBX 3 2 1 0
BITMAP
E0
A0
ENDCHAR
STARTCHAR unnamed
ENCODING -1
BBX 10 4 0 -1
BITMAP
FFC0
FFC0
FFC0
FFC0
ENDCHAR
STARTCHAR A
ENCODING 65
BBX 12 1 -1 2
BITMAP
FFF0
ENDCHAR
ENDFONT
"""


def test_small():
    width, height, glyphs = bdf2psf.parse_bdf(SMALL)
    check((width, height, len(glyphs)) == (10, 4, 2), "10x4, the unencoded glyph left out")
    data = bdf2psf.psf2(width, height, glyphs)
    fields, bitmaps, table = read_psf(data)
    check(fields == (0x864AB572, 0, 32, 1, 2, 8, 4, 10), "the PSF2 header: %r" % (fields,))
    # U+2500: 3x2 at x 1, its bottom on the baseline (row 2 of 4, one row
    # below it): rows 1 and 2.
    check(bitmaps[0] == bytes([0, 0, 0x70, 0, 0x50, 0, 0, 0]),
          "a glyph's box placed by its offsets: %r" % bitmaps[0])
    # 'A': 12 wide from x -1, on row 0: clipped to the 10 columns.
    check(bitmaps[1] == bytes([0xFF, 0xC0, 0, 0, 0, 0, 0, 0]),
          "a glyph wider than the box is clipped: %r" % bitmaps[1])
    check(table == [b"\xe2\x94\x80", b"A"], "the table in UTF-8: %r" % table)


def test_refused():
    for text, what in (
            ("STARTFONT 2.1\nENDFONT\n", "no bounding box"),
            ("FONTBOUNDINGBOX 8 8 0 0\nENDFONT\n", "no glyphs"),
            ("FONTBOUNDINGBOX 8 8 0 0\nSTARTCHAR x\nENCODING 65\nBBX 8 2 0 0\nBITMAP\nFF\nENDCHAR\n",
             "fewer rows than the glyph's box"),
            ("FONTBOUNDINGBOX 8 8 0 0\nSTARTCHAR x\nBBX 8 1 0 0\nBITMAP\nFF\nENDCHAR\n",
             "a glyph with no encoding")):
        try:
            bdf2psf.parse_bdf(text)
            check(False, "refused: " + what)
        except bdf2psf.BdfError:
            check(True, what)


def bdf_glyphs(path):
    """Each encoded glyph of a BDF whose glyphs all fill the box: (code, rows
    as integers)."""
    out, code, rows = [], None, None
    with open(path) as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            if words[0] == "ENCODING":
                code = int(words[1])
            elif words[0] == "BITMAP":
                rows = []
            elif words[0] == "ENDCHAR":
                if code >= 0:
                    out.append((code, rows))
                rows = None
            elif rows is not None:
                rows.append(int(words[0], 16))
    return out


def test_spleen():
    with tempfile.TemporaryDirectory() as tmp:
        for size in ("5x8", "6x12", "8x16", "12x24", "16x32"):
            bdf = os.path.join(ROOT, "third_party", "spleen", "spleen-%s.bdf" % size)
            out = os.path.join(tmp, size + ".psf")
            again = os.path.join(tmp, size + "-again.psf")
            for target in (out, again):
                r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "scripts", "bdf2psf.py"),
                                    bdf, target], capture_output=True, text=True)
                check(r.returncode == 0, "%s converts: %s" % (size, r.stderr))
            with open(out, "rb") as f:
                data = f.read()
            with open(again, "rb") as f:
                check(f.read() == data, "%s: the same output twice" % size)
            fields, bitmaps, table = read_psf(data)
            width, height = (int(n) for n in size.split("x"))
            expected = bdf_glyphs(bdf)
            check(fields[6] == height and fields[7] == width and fields[4] == len(expected),
                  "%s: %dx%d, %d glyphs" % (size, fields[7], fields[6], fields[4]))
            row_bytes = (width + 7) // 8
            same = len(bitmaps) == len(expected)
            for (code, rows), bitmap, entry in zip(expected, bitmaps, table):
                bdf_rows = rows  # left-aligned hex, as many bytes a row as the PSF2's
                got = [int.from_bytes(bitmap[i * row_bytes:(i + 1) * row_bytes], "big")
                       for i in range(height)]
                if got != bdf_rows or entry.decode("utf-8") != chr(code):
                    same = False
                    print("  %s: U+%04X differs" % (size, code))
                    break
            check(same, "%s: every glyph bit for bit, named by its code point" % size)
            # Box drawing in all of them; the shades and the block only from
            # 8x16 on (the console scales its own for the smaller ones).
            names = set(table)
            for cp in (0x3F, 0x41, 0x2500, 0x250C, 0x253C, 0xB7, 0xB0):
                check(chr(cp).encode("utf-8") in names, "%s has U+%04X" % (size, cp))


test_small()
test_refused()
test_spleen()
print("run-font-tests: %d checks, %d failures" % (checks, failures))
sys.exit(1 if failures else 0)
