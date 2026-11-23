#!/usr/bin/env python3
"""make_shirt_sheet.py -- shirt designs as the match players wear them (the PC graphics' sprites).

The game dumps its three player sets as decoded, their kit colour slots untouched
(SWOS_PLAYER_DUMP=<file>: src/color_sprites.c ColorSprites_DumpDosPlayers, with the normal pitch's
match palette and the faces' skin and hair tables: every player here the dark-haired one): team1.dat (plain and
vertical stripes: the shirt's columns alternate slots 10 / 11), team2.dat (coloured sleeves: 10 the
sleeves, 11 the body), team3.dat (hoops: the rows alternate). The first row is the game's four types
exactly as those sets draw them. The rest are new designs, decided pixel by pixel on the same frames:
a frame's torso and sleeves come from team2.dat's slots, its torso's columns and rows are counted
(the front's torso is 5 wide -- so halves are 3 | 2, never even), the back view mirrored (seen from
behind, left is right). Each design in three views -- front, back, side (frames 3, 0, 6) -- on the
grass, in two colours, its name under it.

  SWOS_PLAYER_DUMP=build/players.bin SWOS_SKIP_INTRO=1 bin/SWOS.x86_64          (exits once written)
  python3 scripts/make_shirt_sheet.py build/players.bin dist/shirt_designs.png [--scale 7]
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_kit_sheet import FONT, write_png          # noqa: E402

VIEWS = [("front", 3, False), ("back", 0, True), ("side", 6, False)]
RED, WHITE = (220, 16, 16), (245, 245, 245)
SHORTS, SOCKS = (24, 24, 28), RED          # (black shorts: white ones would melt into the shirts)
SHADOW = 8
TONE = 0                                     # the faces' skin and hair: 0 the dark-haired player


def load(path):
    d = open(path, "rb").read()
    if d[:4] != b"PLYR":
        sys.exit("make_shirt_sheet: not the game's player dump")
    sets, frames = struct.unpack("<II", d[4:12])
    o, out = 12, []
    for _ in range(sets):
        fr = []
        for _ in range(frames):
            w, h, cx, cy = struct.unpack("<4H", d[o:o + 8])
            o += 8
            fr.append((w, h, d[o:o + w * h]))
            o += w * h
        out.append(fr)
    pal = [tuple(d[o + i * 3:o + i * 3 + 3]) for i in range(256)]     # the normal pitch's match palette
    o += 768
    tones = [struct.unpack("<16H", d[o + t * 32:o + (t + 1) * 32]) for t in range(3)]
    return out, pal, tones


# A new design: f(c, r, bw, sleeve, side) -> 1 the first colour, 2 the second.
#   c, r: the pixel's column and row in the torso (0 = its left / top); bw: the torso's width;
#   sleeve: a sleeve pixel; side: -1 left of the torso, 1 right of it (sleeves).
def mid(bw): return bw // 2
def centre_stripe(c, r, bw, s, side):  return 1 if not s and c == mid(bw) else 2
def v_neck(c, r, bw, s, side):         return 2 if not s and (c == r or c == bw - 1 - r) else 1
def checks(c, r, bw, s, side):         return 1 if (c + r) % 2 == 0 else 2
def halves(c, r, bw, s, side):         return (1 if side < 0 else 2) if s else (1 if c <= mid(bw) else 2)
def quarters(c, r, bw, s, side):
    left = side < 0 if s else c <= mid(bw)
    return (1 if left else 2) if r <= 1 else (2 if left else 1)
def sash(c, r, bw, s, side):           return 2 if not s and c in (2 * r - 1, 2 * r) else 1
def chest_band(c, r, bw, s, side):     return 2 if r == 1 else 1
def shoulders(c, r, bw, s, side):      return 2 if s or r == 0 else 1
def top_bottom(c, r, bw, s, side):     return 1 if r <= 1 or s else 2
def nordic_cross(c, r, bw, s, side):   return 2 if not s and (r == 1 or c == mid(bw) - 1) else 1
def side_panels(c, r, bw, s, side):    return 2 if s or c in (0, bw - 1) else 1
def broad_band(c, r, bw, s, side):     return 2 if not s and 0 < c < bw - 1 else 1

NEW = [("centre stripe", centre_stripe), ("v", v_neck), ("checks", checks), ("halves", halves),
       ("quarters", quarters), ("sash", sash), ("chest band", chest_band), ("shoulders", shoulders),
       ("top bottom", top_bottom), ("nordic cross", nordic_cross), ("side panels", side_panels),
       ("broad band", broad_band)]


def current(sets, t, frame):
    """The game's four types: (set, slot 10's colour, slot 11's)."""
    kit = {0: (0, RED, RED), 1: (1, WHITE, RED), 2: (0, RED, WHITE), 3: (2, RED, WHITE)}[t]
    w, h, px = sets[kit[0]][frame]
    return w, h, [[kit[1] if p == 10 else kit[2] if p == 11 else p for p in px[y * w:(y + 1) * w]]
                  for y in range(h)]


def design(sets, f, frame, mirror):
    w, h, base = sets[0][frame]                 # (team1.dat: the shirt's pixels, slots 10 and 11)
    _, _, sl = sets[1][frame]                   # (team2.dat: 10 a sleeve, 11 the torso)
    body = [(x, y) for y in range(h) for x in range(w) if sl[y * w + x] == 11]
    if not body:
        return current(sets, 0, frame)
    bx0, bx1 = min(x for x, _ in body), max(x for x, _ in body)
    by0 = min(y for _, y in body)
    bw = bx1 - bx0 + 1
    out = []
    for y in range(h):
        row = []
        for x in range(w):
            p = base[y * w + x]
            if p in (10, 11):
                sleeve = sl[y * w + x] == 10
                c = x - bx0
                side = -1 if x < bx0 else 1
                if mirror:                      # (seen from behind: the player's left on the right)
                    c, side = bw - 1 - c, -side
                row.append(RED if f(c, y - by0, bw, sleeve, side) == 1 else WHITE)
            else:
                row.append(p)
        out.append(row)
    return w, h, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("png")
    ap.add_argument("--scale", type=int, default=7)
    a = ap.parse_args()
    sets, pal, tones = load(a.dump)
    grass = [pal[0], pal[7]]                    # (the PC pitch's two grass entries)

    items = [(n, [current(sets, t, fr) for _, fr, _ in VIEWS])
             for t, n in enumerate(["plain", "sleeves", "stripes", "hoops"])]
    items += [(n, [design(sets, f, fr, mir) for _, fr, mir in VIEWS]) for n, f in NEW]

    s, ts, cols = a.scale, 2, 4
    pw, ph = 16 * s, 16 * s                     # a view's box
    cw, ch = 3 * pw + 24, ph + 40
    margin = 24
    rows = (len(items) + cols - 1) // cols
    W, H = margin * 2 + cols * cw, margin * 2 + rows * ch + 14
    img = bytearray(W * H * 3)
    for y in range(H):
        g = grass[(y // (ch // 2)) % 2]
        img[y * W * 3:(y + 1) * W * 3] = bytes(g) * W
    conv = tones[TONE]

    def put(x, y, c):
        if 0 <= x < W and 0 <= y < H:
            img[(y * W + x) * 3:(y * W + x) * 3 + 3] = bytes(c)

    def text(x, y, t):
        for dx, c in ((1, (0, 0, 0)), (0, (255, 255, 255))):
            cx = x
            for ch_ in t.upper():
                for r, bits in enumerate(FONT.get(ch_, FONT[" "]).split()):
                    for k, b in enumerate(bits):
                        if b == "1":
                            for i in range(ts):
                                for j in range(ts):
                                    put(cx + k * ts + i + dx * ts, y + r * ts + j + dx * ts, c)
                cx += 6 * ts

    for i, (name, views) in enumerate(items):
        col, row = i % cols, i // cols
        x0, y0 = margin + col * cw, margin + row * ch + (14 if row else 0)
        for v, (w, h, px) in enumerate(views):
            ox, oy = x0 + v * pw + 8, y0
            for y in range(h):
                for x in range(w):
                    p = px[y][x]
                    if p == 0:
                        continue
                    c = p if isinstance(p, tuple) else SHORTS if p == 14 else SOCKS if p == 15 else \
                        pal[conv[p] if p < 16 else p]
                    for i2 in range(s):
                        for j2 in range(s):
                            put(ox + x * s + i2, oy + y * s + j2, c)
        tw = len(name) * 6 * ts - ts
        text(x0 + (cw - tw) // 2, y0 + ph + 8, name)
    for x in range(margin, W - margin):         # the line under the game's own four
        put(x, margin + ch + 6, (255, 255, 255))
    write_png(a.png, W, H, img)
    print("make_shirt_sheet: the game's 4 types + %d new, front / back / side, %d x %d -> %s"
          % (len(NEW), W, H, a.png))


if __name__ == "__main__":
    main()
