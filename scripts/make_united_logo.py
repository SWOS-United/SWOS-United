#!/usr/bin/env python3
"""make_united_logo.py -- SWOS United's banner for the top of menus2020's main menu.

    scripts/make_united_logo.py [--height 31] [--source docs/images/swosunited-banner.png]

The banner ("SWOS", three little players, "UNITED") made HEIGHT pixels tall (its width by its aspect ratio, LANCZOS) as
a 24-bit BMP the menu image loader takes (docs/MENUS.md §13): its pure black nudged to 1,1,1 so it
is not see-through, its clear parts (alpha < 128) magenta. Writes
bin/mods/menus2020/grafs_united/logo.bmp (committed: the logo is ours). The size it prints is
make_menus2020.py's LOGO_SIZE: keep the two the same. Needs Pillow.
"""
import argparse
import os

from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(REPO, "docs", "images", "swosunited-banner.png")
OUT = os.path.join(REPO, "bin", "mods", "menus2020", "grafs_united", "logo.bmp")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--height", type=int, default=31, help="its height in pixels (the band is 40)")
    ap.add_argument("--source", default=SRC, help="the picture (default the banner)")
    args = ap.parse_args()
    im = Image.open(args.source).convert("RGBA")
    w = round(im.width * args.height / im.height)
    im = im.resize((w, args.height), Image.LANCZOS)
    out = Image.new("RGB", im.size)
    src, dst = im.load(), out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = src[x, y]
            dst[x, y] = (255, 0, 255) if a < 128 else (1, 1, 1) if (r, g, b) == (0, 0, 0) else (r, g, b)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    out.save(OUT, "BMP")
    print("  -> %s (%dx%d)" % (os.path.relpath(OUT, REPO), w, args.height))


if __name__ == "__main__":
    main()
