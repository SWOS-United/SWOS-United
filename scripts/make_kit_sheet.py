#!/usr/bin/env python3
"""make_kit_sheet.py -- a sheet of the 50 kit colours, each worn by the little player facing forward.

The game draws the players (SWOS_KIT_SHEET=<file>: src/mod_menus2020.c kit_sheet_dump, the menu
palette's colours, the menus' rainbow order, each colour in the three skin and hair tones); this
lays them out -- five columns of ten down the rainbow, its name under each, the three tones mixed
evenly in random places (--seed: the same sheet each time) -- on the normal pitch's grass
(palette_data.c _normalPalette, its two stripes), enlarged, a band left empty at the top for a logo
(--top), as a PNG.

  SWOS_KIT_SHEET=build/kits.rgba SWOS_SKIP_INTRO=1 bin/SWOS.x86_64     (exits once written)
  python3 scripts/make_kit_sheet.py build/kits.rgba dist/kit_colours.png [--scale 6] [--top 220]
"""
import argparse
import os
import random
import re
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A 5x7 capital font for the names (its own: nothing of the game's is copied).
FONT = {
    "A": "01110 10001 10001 11111 10001 10001 10001", "B": "11110 10001 10001 11110 10001 10001 11110",
    "C": "01110 10001 10000 10000 10000 10001 01110", "D": "11110 10001 10001 10001 10001 10001 11110",
    "E": "11111 10000 10000 11110 10000 10000 11111", "F": "11111 10000 10000 11110 10000 10000 10000",
    "G": "01110 10001 10000 10111 10001 10001 01111", "H": "10001 10001 10001 11111 10001 10001 10001",
    "I": "01110 00100 00100 00100 00100 00100 01110", "J": "00111 00010 00010 00010 00010 10010 01100",
    "K": "10001 10010 10100 11000 10100 10010 10001", "L": "10000 10000 10000 10000 10000 10000 11111",
    "M": "10001 11011 10101 10101 10001 10001 10001", "N": "10001 11001 10101 10011 10001 10001 10001",
    "O": "01110 10001 10001 10001 10001 10001 01110", "P": "11110 10001 10001 11110 10000 10000 10000",
    "Q": "01110 10001 10001 10001 10101 10010 01101", "R": "11110 10001 10001 11110 10100 10010 10001",
    "S": "01111 10000 10000 01110 00001 00001 11110", "T": "11111 00100 00100 00100 00100 00100 00100",
    "U": "10001 10001 10001 10001 10001 10001 01110", "V": "10001 10001 10001 10001 10001 01010 00100",
    "W": "10001 10001 10001 10101 10101 10101 01010", "X": "10001 10001 01010 00100 01010 10001 10001",
    "Y": "10001 10001 01010 00100 00100 00100 00100", "Z": "11111 00001 00010 00100 01000 10000 11111",
    " ": "00000 00000 00000 00000 00000 00000 00000", "-": "00000 00000 00000 11111 00000 00000 00000",
}


def names_in_order():
    """The menus' order's names (kit_variants_table.inc: the comment under kKitMenuOrder)."""
    src = open(os.path.join(REPO, "src", "kit_variants_table.inc")).read()
    m = re.search(r"kKitMenuOrder\[[^\]]*\]\s*=\s*\{[^}]*\};\s*/\*\s*\((.*?)\)\s*\*/", src, re.S)
    if not m:
        sys.exit("make_kit_sheet: no kKitMenuOrder names in src/kit_variants_table.inc")
    return [n.strip() for n in m.group(1).split(",")]


def normal_grass():
    """The normal pitch's grass: palette_data.c _normalPalette's first entries (0x0RGB), 8-bit."""
    src = open(os.path.join(REPO, "src", "palette_data.c")).read()
    m = re.search(r"uint16_t _normalPalette\[9\]\s*=\s*\{(.*?)\}", src, re.S)
    words = [int(w, 16) for w in re.findall(r"0x([0-9A-Fa-f]{4})", m.group(1))][:3]
    return [tuple(((w >> s) & 15) * 17 for s in (8, 4, 0)) for w in words]


def write_png(path, w, h, rgb):
    raw = b"".join(b"\0" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("png")
    ap.add_argument("--scale", type=int, default=6)
    ap.add_argument("--top", type=int, default=220, help="the empty band at the top (pixels), for a logo")
    ap.add_argument("--seed", type=int, default=2026)
    a = ap.parse_args()
    data = open(a.dump, "rb").read()
    if data[:4] != b"KITS":
        sys.exit("make_kit_sheet: not the game's dump")
    n, pw, ph, tones = struct.unpack("<IIII", data[4:20])
    size = pw * ph * 4
    players, off = [], 20               # players[colour][tone]
    for _ in range(n):
        players.append([data[off + 1 + t * size:off + 1 + (t + 1) * size] for t in range(tones)])
        off += 1 + tones * size
    mix = [i % tones for i in range(n)]                        # the tones evenly (17 / 17 / 16) ...
    random.Random(a.seed).shuffle(mix)                         # ... in random places
    names = names_in_order()
    if len(names) != n:
        sys.exit("make_kit_sheet: %d players, %d names" % (n, len(names)))

    cols, rows, s, ts = 5, (n + 4) // 5, a.scale, 2            # (ts: the names' scale)
    cw, ch = 170, ph * s + 40                                  # a cell: the player and its name
    margin = 20
    W, H = margin * 2 + cols * cw, a.top + margin + rows * ch
    grass = normal_grass()
    img = bytearray(W * H * 3)
    stripe = ch // 2
    for y in range(H):                                         # the mowed stripes, across
        g = grass[(y // stripe) % 2]
        for x in range(W):
            img[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(g)

    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            img[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)

    def text(x, y, t):
        for dx, dy, c in ((1, 1, (0, 0, 0)), (0, 0, (255, 255, 255))):     # a shadow, then white
            cx = x
            for ch_ in t.upper():
                rows_ = FONT.get(ch_, FONT[" "]).split()
                for r, bits in enumerate(rows_):
                    for k, b in enumerate(bits):
                        if b == "1":
                            for i in range(ts):
                                for j in range(ts):
                                    put(cx + (k * ts) + i + dx * ts, y + r * ts + j + dy * ts, c)
                cx += 6 * ts

    for i in range(n):
        col, row = i // rows, i % rows                         # a colour family a column
        x0, y0 = margin + col * cw, a.top + row * ch
        px = players[i][mix[i]]
        ox = x0 + (cw - pw * s) // 2
        for y in range(ph):
            for x in range(pw):
                r, g, b, al = px[(y * pw + x) * 4:(y * pw + x) * 4 + 4]
                if al:
                    for i2 in range(s):
                        for j2 in range(s):
                            put(ox + x * s + i2, y0 + 4 + y * s + j2, (r, g, b))
        label = names[i]
        tw = len(label) * 6 * ts - ts
        text(x0 + (cw - tw) // 2, y0 + 8 + ph * s, label)
    write_png(a.png, W, H, img)
    print("make_kit_sheet: %d colours, tones %s, %d x %d (top band %d) -> %s"
          % (n, [mix.count(t) for t in range(tones)], W, H, a.top, a.png))


if __name__ == "__main__":
    main()
