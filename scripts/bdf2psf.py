#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Convert a BDF bitmap font to PSF2, the format the console loads.

    scripts/bdf2psf.py FONT.bdf OUT.psf

Every glyph with a Unicode encoding (ENCODING >= 0, the font being ISO10646)
becomes one PSF2 glyph, in the BDF's order, as large as the font's bounding
box: each glyph's own box is placed in it by its offset from the baseline,
and clipped to it. The PSF2 Unicode table names each glyph's code point.
The output depends only on the input.
"""

import struct
import sys

PSF2_MAGIC = 0x864AB572
PSF2_HAS_UNICODE_TABLE = 0x01
PSF2_SEPARATOR = 0xFF


class BdfError(Exception):
    pass


def parse_bdf(text):
    """Return (width, height, glyphs): glyphs is a list of (code point,
    rows), each row a list of `width` booleans, `height` rows."""
    lines = text.splitlines()
    box = None
    glyphs = []
    i = 0
    while i < len(lines):
        words = lines[i].split()
        i += 1
        if not words:
            continue
        if words[0] == "FONTBOUNDINGBOX":
            box = tuple(int(w) for w in words[1:5])
        elif words[0] == "STARTCHAR":
            if box is None:
                raise BdfError("a glyph before FONTBOUNDINGBOX")
            code, bbx, bitmap = None, None, None
            while i < len(lines):
                words = lines[i].split()
                i += 1
                if not words:
                    continue
                if words[0] == "ENCODING":
                    code = int(words[1])
                elif words[0] == "BBX":
                    bbx = tuple(int(w) for w in words[1:5])
                elif words[0] == "BITMAP":
                    bitmap = []
                    while i < len(lines) and lines[i].strip() != "ENDCHAR":
                        bitmap.append(lines[i].strip())
                        i += 1
                    i += 1  # ENDCHAR
                    break
            if code is None or bbx is None or bitmap is None:
                raise BdfError("a glyph without ENCODING, BBX or BITMAP")
            if code >= 0:
                glyphs.append((code, place(box, bbx, bitmap)))
    if box is None:
        raise BdfError("no FONTBOUNDINGBOX")
    if not glyphs:
        raise BdfError("no glyphs")
    return box[0], box[1], glyphs


def place(box, bbx, bitmap):
    """The glyph's bitmap (its own box) placed in the font's box."""
    width, height, box_x, box_y = box
    w, h, x_off, y_off = bbx
    if len(bitmap) != h:
        raise BdfError("BITMAP has %d rows, BBX says %d" % (len(bitmap), h))
    rows = [[False] * width for _ in range(height)]
    top = (height + box_y) - (y_off + h)  # the glyph's first row, from the cell's top
    left = x_off - box_x
    for r, hexrow in enumerate(bitmap):
        bits = int(hexrow, 16) if hexrow else 0
        nbits = len(hexrow) * 4
        for c in range(w):
            if bits >> (nbits - 1 - c) & 1:
                y, x = top + r, left + c
                if 0 <= y < height and 0 <= x < width:
                    rows[y][x] = True
    return rows


def psf2(width, height, glyphs):
    row_bytes = (width + 7) // 8
    data = bytearray(struct.pack("<8I", PSF2_MAGIC, 0, 32, PSF2_HAS_UNICODE_TABLE,
                                 len(glyphs), row_bytes * height, height, width))
    for _, rows in glyphs:
        for row in rows:
            value = 0
            for x, on in enumerate(row):
                if on:
                    value |= 0x80 << (8 * (row_bytes - 1)) >> x
            data += value.to_bytes(row_bytes, "big")
    for code, _ in glyphs:
        data += chr(code).encode("utf-8") + bytes([PSF2_SEPARATOR])
    return bytes(data)


def main(argv):
    if len(argv) != 3:
        print("usage: bdf2psf.py FONT.bdf OUT.psf", file=sys.stderr)
        return 2
    with open(argv[1], encoding="ascii", errors="replace") as f:
        text = f.read()
    try:
        width, height, glyphs = parse_bdf(text)
    except (BdfError, ValueError) as e:
        print("bdf2psf: %s: %s" % (argv[1], e), file=sys.stderr)
        return 1
    with open(argv[2], "wb") as f:
        f.write(psf2(width, height, glyphs))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
