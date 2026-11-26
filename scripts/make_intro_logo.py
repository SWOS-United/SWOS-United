#!/usr/bin/env python3
"""make_intro_logo.py -- the title picture's logo (packaging/intro/swos-united-logo.png), built on the
title screen's own pixel grid, so scripts/make_loader_logos.sh puts it in 1:1 and nothing is resampled.

    SWOS             the menu logo's upper line (bin/mods/menus2020/grafs_united/logo.png, from
                     scripts/make_united_logo.py), as it is: chrome rim, black edge
    the three men    packaging/intro/players.png, 21 x 12 pixels of art (the original's: sensiblesoccer.de's
                     icon, its colours), each pixel 5 x 5; round them a light rim a pixel wide and a black
                     edge, so their black hair is seen on the screen's dark ground
    UNITED           the menu logo's lower line

stacked, centred, GAP pixels apart. Run scripts/make_loader_logos.sh after it (its size is printed:
make_loader_logos.sh fits the logo into exactly that, the caption's 19 rows added to the height).
"""
import os

import cv2
import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(REPO, "bin", "mods", "menus2020", "grafs_united", "logo.png")
PLAYERS = os.path.join(REPO, "packaging", "intro", "players.png")
OUT = os.path.join(REPO, "packaging", "intro", "swos-united-logo.png")

SCALE = 5               # (pixels) an art pixel of the men
RIM = (200, 200, 200)   # their rim: one flat light grey (the title palette has few greys)
EDGE = (8, 8, 12)       # and the black round it (near-black: make_loader_logos keeps it a black, not 0)
GAP = 4                 # (pixels) between SWOS, the men and UNITED
SPLIT = 33              # the menu logo's row between SWOS and UNITED


def trim(im):
    return im.crop(im.getbbox())


def players():
    art = np.array(Image.open(PLAYERS).convert("RGBA"))
    big = np.repeat(np.repeat(art, SCALE, 0), SCALE, 1)
    pad = 3
    out = np.zeros((big.shape[0] + 2 * pad, big.shape[1] + 2 * pad, 4), np.uint8)
    out[pad:-pad, pad:-pad] = big
    body = np.zeros(out.shape[:2], np.uint8)
    body[pad:-pad, pad:-pad] = big[..., 3] > 0
    rim = cv2.dilate(body, np.ones((3, 3), np.uint8))                           # a pixel round, corners too
    edge = cv2.dilate(rim, cv2.getStructuringElement(cv2.MORPH_CROSS, (3, 3)))  # then a pixel, square corners cut
    out[(rim > 0) & (body == 0)] = RIM + (255,)
    out[(edge > 0) & (rim == 0)] = EDGE + (255,)
    return trim(Image.fromarray(out, "RGBA"))


def main():
    logo = Image.open(LOGO).convert("RGBA")
    w, h = logo.size
    parts = [trim(logo.crop((0, 0, w, SPLIT))), players(), trim(logo.crop((0, SPLIT, w, h)))]
    W = max(p.width for p in parts)
    H = sum(p.height for p in parts) + GAP * (len(parts) - 1)
    pic = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    y = 0
    for p in parts:
        pic.alpha_composite(p, ((W - p.width) // 2, y))
        y += p.height + GAP
    pic.save(OUT)
    print("  -> %s (%dx%d)" % (os.path.relpath(OUT, REPO), W, H))


if __name__ == "__main__":
    main()
