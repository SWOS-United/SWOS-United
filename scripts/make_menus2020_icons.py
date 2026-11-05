#!/usr/bin/env python3
"""make_menus2020_icons.py -- SWOS 2020's main-menu icons as BMPs for the menus2020 mod (SWOS United).

    scripts/make_menus2020_icons.py [--swos2020 DIR] [--assets DIR]

SWOS 2020 draws a 16x16 icon on each of its six main-menu buttons (game.cpp, the main menu's
onDraw: drawMapEx256_8sets(main_menu_icon_v3_16x16_8x1_map, ...)). The pictures are palette
indices in its source (src/PixelmapAndStrings.h), into the PC game's menu palette -- the one
FILL.256 (the menu background) carries -- with 0xE9 (the green) as see-through.

OpenSWOS's JSON menus draw an entry's "image" (docs/MENUS.md §13): a .bmp, each colour to the
nearest of the menu palette, magenta (and pure black) see-through. So this writes each icon as
a 24-bit BMP in the PC palette's colours, magenta where SWOS 2020 drew nothing, into the mod's
overlay: bin/mods/menus2020/grafs_2020/menus2020/<name>.bmp (scripts/make_menus2020.py's menu
names them).

The icons are SWOS 2020's art, so like its pitches (grafs_2020) they are made on the player's
machine and never committed (.gitignore). Without them the menu shows its buttons without icons.
"""
import argparse
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "bin", "mods", "menus2020", "grafs_2020", "menus2020")
DEFAULT_SWOS2020 = "/mnt/c/Projects/GitHub/swos-2020"
DEFAULT_ASSETS = os.environ.get("SWOS_ASSETS", os.path.expanduser("~/Documents/SWOS/assets"))

# main_menu_icon_v3_16x16_8x1_map: eight 16x16 icons side by side; SWOS 2020's order.
ICONS = ["compete", "practice", "replays", "options", "editing", "extras", "multiplayer"]
SEE_THROUGH = 0xE9


def read_map(header, name, rows, cols):
    with open(header, encoding="latin-1") as f:
        src = f.read()
    m = re.search(r"\b%s\s*\[%d\]\s*\[%d\]\s*=\s*\{(.*?)\};" % (re.escape(name), rows, cols), src, re.S)
    if not m:
        sys.exit("make_menus2020_icons: %s not found in %s" % (name, header))
    vals = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(1))]
    if len(vals) != rows * cols:
        sys.exit("make_menus2020_icons: %s has %d values, not %d" % (name, len(vals), rows * cols))
    return [vals[r * cols:(r + 1) * cols] for r in range(rows)]


def read_palette(assets):
    for name in ("FILL.256", "fill.256"):
        path = os.path.join(assets, "grafs_dos", name)
        if os.path.exists(path):
            data = open(path, "rb").read()
            pal = data[64000:64000 + 768]
            if len(pal) == 768:
                return [tuple(c * 255 // 63 for c in pal[i * 3:i * 3 + 3]) for i in range(256)]
    sys.exit("make_menus2020_icons: no grafs_dos/FILL.256 in %s (run OpenSWOS Setup with the PC game)"
             % assets)


def write_bmp(path, pixels):
    """pixels: rows of (r, g, b), top first -> a 24-bit bottom-up BMP."""
    h, w = len(pixels), len(pixels[0])
    stride = (w * 3 + 3) & ~3
    body = bytearray()
    for row in reversed(pixels):
        line = bytearray()
        for r, g, b in row:
            line += bytes((b, g, r))
        body += line + b"\0" * (stride - len(line))
    head = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    with open(path, "wb") as f:
        f.write(head + info + body)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--swos2020", default=DEFAULT_SWOS2020, help="swos-2020 checkout")
    ap.add_argument("--assets", default=DEFAULT_ASSETS, help="the assets folder (grafs_dos/FILL.256)")
    args = ap.parse_args()

    icons = read_map(os.path.join(args.swos2020, "src", "PixelmapAndStrings.h"),
                     "main_menu_icon_v3_16x16_8x1_map", 16, 128)
    pal = read_palette(args.assets)
    os.makedirs(OUT, exist_ok=True)
    for n, name in enumerate(ICONS):
        pixels = [[(255, 0, 255) if v == SEE_THROUGH else pal[v]
                   for v in row[n * 16:(n + 1) * 16]] for row in icons]
        path = os.path.join(OUT, name + ".bmp")
        write_bmp(path, pixels)
        print("  -> %s" % os.path.relpath(path, REPO))


if __name__ == "__main__":
    main()
