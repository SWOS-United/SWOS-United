#!/usr/bin/env python3
"""make_wizard_images.py -- the Windows installer's two wizard pictures (packaging/windows).

  WizardImage*.bmp       the tall banner down the left of the welcome and finished pages: a
                         full-height strip of the menus' purple picture
                         (bin/mods/backgrounds/images/SWOS_United.png), centred on its ball
  WizardSmallImage*.bmp  the badge at the top right of every other page: SWOS United's logo
                         (packaging/intro/swos-united-logo.png) on the page's white

Each at Inno Setup's 100% size and the larger DPI steps (swos.iss lists them; Inno picks the one
nearest the screen's scale), 24-bit BMP -- Inno will not take PNG. Needs Pillow (Cygwin's python3
has it: /cygdrive/c/cygwin64/bin/python3.9.exe scripts/make_wizard_images.py).
"""
import os
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
OUT = os.path.join(ROOT, "packaging", "windows")
BACKDROP = os.path.join(ROOT, "bin", "mods", "backgrounds", "images", "SWOS_United.png")
LOGO = os.path.join(ROOT, "packaging", "intro", "swos-united-logo.png")

SCALES = [(100, ""), (125, "125"), (150, "150"), (175, "175"), (200, "200"), (250, "250")]
BIG = (164, 314)
SMALL = (55, 58)
BALL_X = 0.51                       # the ball's centre, a fraction of the picture's width


def banner(w, h):
    src = Image.open(BACKDROP).convert("RGB")
    sw, sh = src.size
    cw = round(sh * w / h)          # the strip at the banner's shape, the picture's full height
    x0 = max(0, min(sw - cw, round(sw * BALL_X - cw / 2)))
    return src.crop((x0, 0, x0 + cw, sh)).resize((w, h), Image.LANCZOS)


def badge(w, h):
    logo = Image.open(LOGO).convert("RGBA")
    bb = logo.getbbox()
    if bb:
        logo = logo.crop(bb)
    pad = max(1, round(min(w, h) * 0.04))
    k = min((w - 2 * pad) / logo.width, (h - 2 * pad) / logo.height)
    lw, lh = max(1, round(logo.width * k)), max(1, round(logo.height * k))
    logo = logo.resize((lw, lh), Image.LANCZOS)
    out = Image.new("RGB", (w, h), (255, 255, 255))
    out.paste(logo, ((w - lw) // 2, (h - lh) // 2), logo)
    return out


def main():
    for pct, suffix in SCALES:
        for name, size, make in (("WizardImage", BIG, banner), ("WizardSmallImage", SMALL, badge)):
            w, h = (round(v * pct / 100) for v in size)
            path = os.path.join(OUT, "%s%s.bmp" % (name, suffix))
            make(w, h).save(path, "BMP")
            print("%-24s %4d x %4d" % (os.path.basename(path), w, h))


if __name__ == "__main__":
    main()
