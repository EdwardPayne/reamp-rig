#!/usr/bin/env python3
"""Generates the Reamp Rig app icon as PNG files (no dependencies beyond Python 3).

The icon repeats the top-bar logo without text: a black square (theme `bg`) with a 1 px-style
border in `line`, and the accent-orange mark (theme `accent`) in its centre. Sharp corners,
no gradients, no shadows (PROMPT.md section 5). The square follows the macOS icon grid
(824 of 1024 px, transparent margin) so it sits at the same size as other Dock icons.

    python3 Assets/Icon/make_icon.py            # writes Assets/Icon/icon_<size>.png

CMakeLists.txt passes icon_1024.png (ICON_BIG) and icon_32.png (ICON_SMALL) to
juce_add_gui_app, which builds the bundle's .icns from them. Re-run after changing the
colours in Source/UI/Theme.h and commit the PNGs.
"""

import os
import struct
import zlib

SIZES = (16, 32, 64, 128, 256, 512, 1024)

# Source/UI/Theme.h
BG = (0x00, 0x00, 0x00, 0xFF)
LINE = (0x28, 0x28, 0x2C, 0xFF)
ACCENT = (0xFF, 0x5A, 0x36, 0xFF)
CLEAR = (0, 0, 0, 0)


def layout(size):
    """Pixel rectangles (x0, y0, x1, y1), end exclusive, for one icon size."""
    margin = round(size * 100 / 1024)            # macOS grid: 824 px body on a 1024 canvas
    body = (margin, margin, size - margin, size - margin)
    border = max(1, round(size * 8 / 1024))      # reads as a hairline at every size
    side = body[2] - body[0]
    mark_side = max(2, round(side * 0.30))
    if (side - mark_side) % 2:                   # keep the mark exactly centred
        mark_side += 1
    m0 = body[0] + (side - mark_side) // 2
    mark = (m0, m0, m0 + mark_side, m0 + mark_side)
    return body, border, mark


def render(size):
    body, border, mark = layout(size)
    rows = []
    for y in range(size):
        row = bytearray([0])                     # PNG filter type 0 per row
        for x in range(size):
            if not (body[0] <= x < body[2] and body[1] <= y < body[3]):
                px = CLEAR
            elif mark[0] <= x < mark[2] and mark[1] <= y < mark[3]:
                px = ACCENT
            elif (x < body[0] + border or x >= body[2] - border
                  or y < body[1] + border or y >= body[3] - border):
                px = LINE
            else:
                px = BG
            row.extend(px)
        rows.append(bytes(row))
    return b"".join(rows)


def png(size, pixels):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)   # 8-bit RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(pixels, 9)) + chunk(b"IEND", b""))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    for size in SIZES:
        path = os.path.join(here, "icon_%d.png" % size)
        with open(path, "wb") as f:
            f.write(png(size, render(size)))
        body, border, mark = layout(size)
        print("%s  body %s border %d mark %s" % (os.path.basename(path), body, border, mark))


if __name__ == "__main__":
    main()
