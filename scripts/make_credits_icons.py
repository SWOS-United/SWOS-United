#!/usr/bin/env python3
"""Build bin/data/credits_icons.bmp -- the photos the CREDITS screens pick from.

A strip of seven 40x40 photos. Each screen chooses its own from it, by place
(include/credits_text.h CREDITS_STRIP_*: src/credits_menu.c on SDL, and
make_credits_aga.py's PHOTOS for the Amiga's backdrop):

  * six from swos-2020/developer_icons/dev_icons_40px_truecolor.bmp (a 3x3 grid of
    40x40 cells, reading order, the last row unused) -- its indexed sheet uses its
    own palette, so they are rebuilt from the truecolour one;
  * benbaker76's, second -- after PLAYAVELI -- from a square portrait given with
    --first (the option keeps its name; the place is PORTRAIT_AT below).

The result is ONE 8-bit image, 40 px tall and 40 px a photo wide, in strip order.
Its pixels are already the palette indices the game shows them with: 128..255, a
block the menu screens do not use (menus use 0..31), which the CREDITS screen lends to
the photos while it is open and gives back when it closes (credits_menu.c). Index 0
is never used, so a pixel is always part of a photo. The colours are 12-bit, as the
Amiga-style palette stores them (4 bits a channel), quantized together to 128.

Beside it, bin/data/credits_icons.png: the same strip in its own colours (2026-11-25), the
six from the 200 px truecolour sheet (dev_icons_200px_truecolor.bmp, the same 3x3 grid) made
40 px, benbaker76's from --first -- or, without it, as he is in the 8-bit strip. The SDL build
shows these over the 8-bit ones (credits_menu.c CreditsMenu_PhotosPresent, any renderer); the
8-bit strip stays for the Amiga and as the fallback.

  scripts/make_credits_icons.py [--first PORTRAIT.jpg] [--swos2020 DIR] [-o OUT] [--png PNG]

Without --first only the PNG is written (benbaker76's from the 8-bit strip there).
"""
import argparse
import os
import sys

from PIL import Image

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CELL = 40
BASE = 128              # first palette index the photos use
COLOURS = 128           # 128..255
SWOS2020_ORDER = [(0, 0), (1, 0), (2, 0), (0, 1), (1, 1), (2, 1)]   # PLAYAVELI .. ELMICHAJ
PORTRAIT_AT = 1         # where --first's photo goes: after PLAYAVELI (credits_text.h CREDITS_STRIP_*)


def square(img):
    w, h = img.size
    s = min(w, h)
    return img.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--first", help="OpenSWOS developer's portrait (any size, cropped square); "
                                    "without it the 8-bit strip is kept and only the PNG made")
    ap.add_argument("--swos2020", default=os.environ.get("SWOS2020_DIR", "/mnt/c/Projects/GitHub/swos-2020"))
    ap.add_argument("-o", "--out", default=os.path.join(REPO, "bin", "data", "credits_icons.bmp"))
    ap.add_argument("--png", default=os.path.join(REPO, "bin", "data", "credits_icons.png"))
    args = ap.parse_args()

    # the strip in its own colours: the six from the 200 px sheet, benbaker76's as he is
    big_path = os.path.join(args.swos2020, "developer_icons", "dev_icons_200px_truecolor.bmp")
    big = Image.open(big_path).convert("RGB")
    if big.size != (600, 600):
        sys.exit("make_credits_icons: %s is %dx%d, expected 600x600" % ((big_path,) + big.size))
    photos = [big.crop((col * 200, row * 200, (col + 1) * 200, (row + 1) * 200)).resize((CELL, CELL), Image.LANCZOS)
              for col, row in SWOS2020_ORDER]
    if args.first:
        first = square(Image.open(args.first).convert("RGB")).resize((CELL, CELL), Image.LANCZOS)
    else:
        first = Image.open(args.out).convert("RGB").crop((PORTRAIT_AT * CELL, 0, (PORTRAIT_AT + 1) * CELL, CELL))
    photos.insert(PORTRAIT_AT, first)
    strip = Image.new("RGB", (CELL * len(photos), CELL))
    for i, p in enumerate(photos):
        strip.paste(p, (i * CELL, 0))
    strip.save(args.png)
    print("make_credits_icons: %d photos in their own colours -> %s" % (len(photos), os.path.relpath(args.png, REPO)))
    if not args.first:
        return

    sheet_path = os.path.join(args.swos2020, "developer_icons", "dev_icons_40px_truecolor.bmp")
    sheet = Image.open(sheet_path).convert("RGB")
    if sheet.size != (3 * CELL, 3 * CELL):
        sys.exit("make_credits_icons: %s is %dx%d, expected 120x120" % ((sheet_path,) + sheet.size))

    photos = [sheet.crop((col * CELL, row * CELL, (col + 1) * CELL, (row + 1) * CELL))
              for col, row in SWOS2020_ORDER]
    photos.insert(PORTRAIT_AT,
                  square(Image.open(args.first).convert("RGB")).resize((CELL, CELL), Image.LANCZOS))

    strip = Image.new("RGB", (CELL * len(photos), CELL))
    for i, p in enumerate(photos):
        strip.paste(p, (i * CELL, 0))
    # 12-bit first, so the quantizer and its dithering see the colours the game can show.
    strip = strip.point(lambda v: ((v + 8) // 17) * 17)
    q = strip.quantize(colors=COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG)

    pal = q.getpalette()[:COLOURS * 3]
    pal = [((v + 8) // 17) * 17 for v in pal]
    pal += [0] * (COLOURS * 3 - len(pal))
    out = Image.new("P", strip.size)
    out.putpalette([0, 0, 0] * BASE + pal)
    flat = q.get_flattened_data() if hasattr(q, "get_flattened_data") else q.getdata()
    out.putdata([BASE + v for v in flat])
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    out.save(args.out)
    print("make_credits_icons: %d photos, %d colours -> %s" % (len(photos), COLOURS, os.path.relpath(args.out, REPO)))


if __name__ == "__main__":
    main()
