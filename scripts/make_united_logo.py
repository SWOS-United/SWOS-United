#!/usr/bin/env python3
"""make_united_logo.py -- SWOS United's logo for the top of menus2020's main menu.

    scripts/make_united_logo.py [--svg out.svg] [--preview out.png]

"SWOS" in the colours of the Sensible title (each letter's colour from the top, lightening into
white, then down into the dark, a chrome rim) over "UNITED" white to grey in a red rim -- as the
Amiga's title stands over SOCCER, SWOS as wide as its "Sensible" (STACKED; side by side as well).
Built on the picture's own pixel grid: the capitals CAP pixels tall (200 units), UNITED's straight
edges, each rim's steps and the lines on whole pixels, so it comes out sharp at its size; only
the curves and slants are shaded by how much of a pixel they cover (16 x 16 samples a pixel).
The edges' alpha is kept: a PNG the menu shows over whatever is under it (menu_extras.c,
MenuExtras_Present). Writes bin/mods/menus2020/grafs_united/logo.png (committed: the logo is
ours); the size it prints is make_menus2020.py's LOGO_SIZE: keep the two the same. Needs Pillow
and numpy only.

--svg writes the same drawing as an SVG (to look at, or for a bigger copy); --preview a 3x view
of it over a dark and a green ground.
"""
import argparse
import math
import os
import re

import numpy as np
from PIL import Image, ImageDraw

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "bin", "mods", "menus2020", "grafs_united", "logo.png")

CAP = 27                # (pixels) the capitals' height: SWOS as wide as the Amiga title's "Sensible"
PX = 200 / CAP          # units a pixel (the letters are drawn 200 units tall)
SS = 16                 # samples a pixel, each way
TOP = 3 * PX            # the capitals' top: 3 pixels for the rims above, 3 below
STACKED = True          # SWOS over UNITED (as the Amiga's title over SOCCER); else side by side
LINE_GAP = 6            # (pixels) stacked: between SWOS's capitals and UNITED's
H = 3 + CAP + LINE_GAP + CAP + 3 if STACKED else CAP + 6     # the picture's height

# ---------------------------------------------------------------- SWOS: the letters' outlines
# (units; capitals 0..200, the round letters 4 over; the S's ends cut upright)
S_PATH = ("M174,3 C158,7 118,-4 72,-4 C32.256,-4 0,21.984 0,54 C0,90 12.692,108.562 46,115.387 "
          "A335.436,335.436 0 0 0 101.628,122.011 A18,18 0 0 1 118.989,140.628 "
          "A18,18 0 0 1 100.372,157.989 A371.436,371.436 0 0 1 40,150.904 "
          "C27.256,148.337 16.235,145.543 10,139.052 L10,197 C26,193 66,204 112,204 "
          "C151.744,204 184,178.016 184,146 C184,110 171.308,91.438 138,84.613 "
          "A335.436,335.436 0 0 0 82.372,77.989 A18,18 0 0 1 65.011,59.372 "
          "A18,18 0 0 1 83.628,42.011 A371.436,371.436 0 0 1 144,49.096 "
          "C156.744,51.663 167.765,54.457 174,60.948 Z")
W_PATH = ("M0,0 H62.9 L96.36,105.3 L125.1,0 H188 L216.76,105.3 L250.2,0 H313.1 L249.6,200 "
          "H186.7 L156.55,89.7 L126.45,200 H63.5 Z")
O_PATH = ("M0,100 A111,104 0 1 1 222,100 A111,104 0 1 1 0,100 Z "
          "M61,100 A50,49 0 1 0 161,100 A50,49 0 1 0 61,100 Z")
SWOS_X = 3 * PX
CHROME_W = 1.5          # (pixels) SWOS's chrome rim
EDGE_W = 1.0            # (pixels) the black edge round it
SPREAD = PX             # (units) more room between SWOS's letters than the drawing had: one pixel
SWOS = [(k, d, x + i * SPREAD) for i, (k, d, x) in
        enumerate([("S", S_PATH, 0.0), ("W", W_PATH, 181.0), ("O", O_PATH, 481.1), ("S2", S_PATH, 706.1)])]
SWOS_W = 890.1 + 3 * SPREAD
# each letter: its colour, the glow and the pastel it lightens through into white
COLOURS = {"S": ("#f80000", "#ff4a3a", "#ffc8c0", "#ba0000"), "W": ("#fbc900", "#ffdc30", "#fff2a8", "#cf9400"),
           "O": ("#0026f9", "#3a7cff", "#b8e4ff", "#001cbb"), "S2": ("#06a606", "#3ad23a", "#c4f5c4", "#047c04")}


def rows(stops):        # (the stops' rows are a 25-pixel capital's: at CAP's)
    return [(r * CAP / 25, c) for r, c in stops]


def swos_face(base, glow, pastel, top=None):     # (pixel rows of the capitals, colour)
    """`top`: a darker colour at the very top, into `base` by row 5 (apart from the light rim)."""
    head = [(-1, top), (5, base)] if top else [(-1, base)]
    return rows(head + [(8, base), (9.5, glow), (10.5, pastel), (11.5, "#ffffff"), (15, "#ffffff"),
                        (18, "#a8a8a8"), (22, "#0c0c0c"), (26, "#060606")])


CHROME = rows([(-3, "#ffffff"), (10, "#d4d4d4"), (12, "#6e6e6e"), (15, "#a0a0a0"), (21, "#f0f0f0"),
               (28, "#c0c0c0")])
UFACE = rows([(0, "#fcfcfc"), (11, "#f2f2f2"), (25, "#9a9a9a")])


# ---------------------------------------------------------------- UNITED, on whole pixels
def P(v):
    return v * PX


# (each letter in whole pixels for 27-pixel capitals: U, T, E stems 8, N and I 7, bars 7)
assert CAP == 27, "UNITED is drawn for 27-pixel capitals"


def u_letter():
    w, st, ch, cb = 21, 8, 4, 21
    return w, [[(0, 0), (st, 0), (st, cb), (w - st, cb), (w - st, 0), (w, 0), (w, CAP - ch), (w - ch, CAP),
                (ch, CAP), (0, CAP - ch)]]


def n_letter():
    w, st = 19, 7
    return w, [[(0, 0), (st, 0), (w - st, 12), (w - st, 0), (w, 0), (w, CAP), (w - st, CAP), (st, 15),
                (st, CAP), (0, CAP)]]


def i_letter():
    return 7, [[(0, 0), (7, 0), (7, CAP), (0, CAP)]]


def t_letter():
    w, st, bar = 16, 8, 7
    a = (w - st) / 2
    return w, [[(0, 0), (w, 0), (w, bar), (w - a, bar), (w - a, CAP), (a, CAP), (a, bar), (0, bar)]]


def e_letter():
    w, st, mw = 18, 8, 16
    return w, [[(0, 0), (w, 0), (w, 7), (st, 7), (st, 11), (mw, 11), (mw, 16), (st, 16), (st, 20), (w, 20),
                (w, CAP), (0, CAP)]]


def d_letter():
    w, ch, c0, c1 = 22, 4, 8, 13
    return w, [[(0, 0), (w - ch, 0), (w, ch), (w, CAP - ch), (w - ch, CAP), (0, CAP)],
               [(c0, 7), (c0, 20), (c1, 20), (c1, 7)]]


U_GAP = 3
U_WIDTH = 21 + 19 + 7 + 16 + 18 + 22 + 5 * U_GAP   # (pixels) UNITED's letters and gaps
# (pixels) where UNITED starts and its capitals' top below TOP: stacked, centred under SWOS
# (on a whole pixel), a line lower; else beside it
U_X = round((SWOS_X + SWOS_W / 2) / PX - U_WIDTH / 2) if STACKED else round((SWOS_X + SWOS_W + 80) / PX)
U_Y = CAP + LINE_GAP if STACKED else 0


def united():
    out, x = [], U_X
    for fn in (u_letter, n_letter, i_letter, t_letter, e_letter, d_letter):
        w, contours = fn()
        out.append([[(P(x + px), P(py)) for px, py in c] for c in contours])
        x += w + U_GAP
    return out, x - U_GAP


# ---------------------------------------------------------------- outlines
def arc_points(p0, rx, ry, phi, large, sweep, p1, n=48):
    """An SVG elliptical arc as points (endpoint to centre form, SVG 1.1 F.6.5)."""
    x1, y1 = p0
    x2, y2 = p1
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        rx, ry = rx * math.sqrt(lam), ry * math.sqrt(lam)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) * (-1 if large == sweep else 1)
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2

    def ang(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    elif sweep and dt < 0:
        dt += 2 * math.pi
    pts = []
    for i in range(1, n + 1):
        t = t1 + dt * i / n
        x, y = rx * math.cos(t), ry * math.sin(t)
        pts.append((cp * x - sp * y + cx, sp * x + cp * y + cy))
    return pts


def path_contours(d, dx=0.0, dy=0.0, n=32):
    """The absolute commands M L H V C A Z, as closed point lists (units, moved by dx, dy)."""
    toks = re.findall(r"[MLHVCAZ]|-?\d*\.?\d+(?:e-?\d+)?", d)
    out, cur, pos, cmd = [], [], (0.0, 0.0), None
    i = 0

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v
    while i < len(toks):
        if toks[i].isalpha():
            cmd = toks[i]
            i += 1
            if cmd == "Z":
                if cur:
                    out.append(cur)
                cur = []
                continue
        if cmd == "M":
            pos = (num(), num())
            cur = [pos]
            cmd = "L"
        elif cmd == "L":
            pos = (num(), num())
            cur.append(pos)
        elif cmd == "H":
            pos = (num(), pos[1])
            cur.append(pos)
        elif cmd == "V":
            pos = (pos[0], num())
            cur.append(pos)
        elif cmd == "C":
            c1, c2, p3 = (num(), num()), (num(), num()), (num(), num())
            p0 = pos
            for k in range(1, n + 1):
                t = k / n
                mt = 1 - t
                cur.append((mt ** 3 * p0[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t ** 3 * p3[0],
                            mt ** 3 * p0[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t ** 3 * p3[1]))
            pos = p3
        elif cmd == "A":
            rx, ry, rot, large, sweep = num(), num(), num(), num(), num()
            p1 = (num(), num())
            cur += arc_points(pos, rx, ry, math.radians(rot), int(large), int(sweep), p1)
            pos = p1
    if cur:
        out.append(cur)
    return [[(x + dx, y + dy) for x, y in c] for c in out]


def area(c):
    return sum(c[i - 1][0] * c[i][1] - c[i][0] * c[i - 1][1] for i in range(len(c))) / 2


def inside(pt, c):
    x, y, r = pt[0], pt[1], False
    for i in range(len(c)):
        (x1, y1), (x2, y2) = c[i - 1], c[i]
        if (y1 > y) != (y2 > y) and x < x1 + (y - y1) * (x2 - x1) / (y2 - y1):
            r = not r
    return r


def mitre(contours, r):
    """The filled region (even-odd) grown by r with sharp corners: each contour moved r away from
    the inside, its corners where the moved sides meet."""
    out = []
    for c in contours:
        hole = sum(inside(c[0], o) for o in contours if o is not c) % 2 == 1
        s = 1 if (area(c) > 0) != hole else -1      # the side away from the filled region
        n = len(c)
        pts = []
        for i in range(n):
            a, b, d = c[i - 1], c[i], c[(i + 1) % n]

            def nrm(p, q):
                ex, ey = q[0] - p[0], q[1] - p[1]
                L = math.hypot(ex, ey)
                return (s * ey / L, -s * ex / L)
            n1, n2 = nrm(a, b), nrm(b, d)
            k = 1 + n1[0] * n2[0] + n1[1] * n2[1]
            pts.append((b[0] + r * (n1[0] + n2[0]) / k, b[1] + r * (n1[1] + n2[1]) / k))
        out.append(pts)
    return out


# ---------------------------------------------------------------- drawing
class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w * SS, h * SS
        self.rgb = np.zeros((self.h, self.w, 3))      # premultiplied
        self.a = np.zeros((self.h, self.w))

    def mask(self, contours, grow=0.0):
        """Coverage of the contours (units; even-odd), grown by `grow` units round about."""
        k = SS / PX
        m = np.zeros((self.h, self.w), bool)
        for c in contours:
            img = Image.new("1", (self.w, self.h), 0)
            ImageDraw.Draw(img).polygon([(x * k, y * k) for x, y in c], fill=1)
            m ^= np.asarray(img, bool)
        if grow > 0:
            img = Image.new("1", (self.w, self.h), 0)
            dr = ImageDraw.Draw(img)
            rr = grow * k
            for c in contours:
                pts = [(x * k, y * k) for x, y in c]
                dr.line(pts + [pts[0]], fill=1, width=int(round(2 * rr)), joint="curve")
                for x, y in pts:
                    dr.ellipse((x - rr, y - rr, x + rr, y + rr), fill=1)
            m |= np.asarray(img, bool)
        return m

    def paint(self, m, colour):
        """Over what is there: m (bool) in colour, one colour (3,) or one a row (h, 3)."""
        col = np.broadcast_to(colour if np.ndim(colour) == 1 else colour[:, None, :], self.rgb.shape)
        self.rgb[m] = col[m]
        self.a[m] = 1.0

    def gradient(self, stops, below=0):
        """A column of colours, the stops at pixel rows of the capitals (row 0 at TOP)."""
        ys = (np.arange(self.h) + 0.5) / SS - TOP / PX - below
        rows = np.array([r for r, _ in stops], float)
        cols = np.array([hexrgb(c) for _, c in stops])
        return np.stack([np.interp(ys, rows, cols[:, i]) for i in range(3)], 1)

    def pixels(self, w, h):
        rgb = self.rgb.reshape(h, SS, w, SS, 3).mean((1, 3))
        a = self.a.reshape(h, SS, w, SS).mean((1, 3))
        straight = np.where(a[..., None] > 0, rgb / np.maximum(a[..., None], 1e-9), 0)
        out = np.zeros((h, w, 4), np.uint8)
        out[..., :3] = np.clip(straight * 255 + 0.5, 0, 255)
        out[..., 3] = np.clip(a * 255 + 0.5, 0, 255)
        return out


def hexrgb(c):
    return np.array([int(c[i:i + 2], 16) / 255 for i in (1, 3, 5)])


def build():
    uni, uend = united()
    W = math.ceil((2 * SWOS_X + SWOS_W) / PX) if STACKED else uend + 3
    cv = Canvas(W, H)
    # SWOS: each letter whole -- a black edge a pixel wide, the chrome rim, its face -- back to
    # front, so each one's black edge parts it from the one before
    chrome = cv.gradient(CHROME)
    for k, d, x in SWOS:
        c = path_contours(d, SWOS_X + x, TOP)
        cv.paint(cv.mask(c, (CHROME_W + EDGE_W) * PX), hexrgb("#000000"))
        cv.paint(cv.mask(c, CHROME_W * PX), chrome)
        cv.paint(cv.mask(c), cv.gradient(swos_face(*COLOURS[k])))
    # UNITED: a black edge, dark red, bright red (a pixel each), the face
    for grow, colour in ((2.5, "#000000"), (2.0, "#8a0000"), (1.0, "#ff1c1c")):
        m = np.zeros((cv.h, cv.w), bool)
        for letter in uni:
            m |= cv.mask(mitre([[(x, y + TOP + U_Y * PX) for x, y in c] for c in letter], grow * PX))
        cv.paint(m, hexrgb(colour))
    m = np.zeros((cv.h, cv.w), bool)
    for letter in uni:
        m |= cv.mask([[(x, y + TOP + U_Y * PX) for x, y in c] for c in letter])
    cv.paint(m, cv.gradient(UFACE, U_Y))
    return cv.pixels(W, H), uni


def svg_of(W, uni):
    """The same drawing as an SVG: each SWOS letter whole (black edge, chrome, face), then UNITED."""
    def stops(lst):
        return "".join('<stop offset="%.4f" stop-color="%s"/>' % ((r * PX) / 200, c) for r, c in lst)

    def grad(gid, lst):
        return ('<linearGradient id="%s" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="0" y2="200">%s'
                '</linearGradient>' % (gid, stops(lst)))

    def lay(body, col, half, join):
        return ('<g fill="%s" stroke="%s" stroke-width="%g" stroke-linejoin="%s" stroke-miterlimit="4">%s</g>'
                % (col, col, 2 * half, join, body))
    defs = [grad("f_" + k, swos_face(*v)) for k, v in COLOURS.items()]
    defs += [grad("chrome", CHROME), grad("uface", UFACE)]
    sw = []
    for k, d, x in SWOS:
        path = '<path transform="translate(%s,0)" d="%s"/>' % (x, d)
        sw.append(lay(path, "#000", (CHROME_W + EDGE_W) * PX, "round") + lay(path, "url(#chrome)", CHROME_W * PX, "round")
                  + '<path transform="translate(%s,0)" fill="url(#f_%s)" d="%s"/>' % (x, k, d))
    ud = "".join('<path d="%s"/>' % " ".join("M" + " L".join("%g,%g" % p for p in c) + " Z" for c in letter)
                 for letter in uni)
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d"><defs>%s</defs>'
            '<g transform="translate(%d,%d)" fill-rule="evenodd">%s</g>'
            '<g transform="translate(0,%d)" fill-rule="evenodd">%s%s%s<g fill="url(#uface)">%s</g></g></svg>\n'
            % (W * PX, H * PX, W * PX, H * PX, "".join(defs), SWOS_X, TOP, "".join(sw), TOP + U_Y * PX,
               lay(ud, "#000", 2.5 * PX, "miter"), lay(ud, "#8a0000", 2 * PX, "miter"),
               lay(ud, "#ff1c1c", PX, "miter"), ud))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--svg", help="also write the drawing as an SVG here")
    ap.add_argument("--preview", help="also write a 3x preview over a dark and a green ground here")
    args = ap.parse_args()
    px, uni = build()
    h, w = px.shape[:2]
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    Image.fromarray(px, "RGBA").save(OUT)
    print("  -> %s (%dx%d)" % (os.path.relpath(OUT, REPO), w, h))
    if args.svg:
        open(args.svg, "w").write(svg_of(w, uni))
    if args.preview:
        rows = []
        for bg in ((20, 20, 28), (60, 90, 60)):
            a = px[..., 3:4] / 255.0
            rows.append(np.clip(px[..., :3] * a + np.array(bg) * (1 - a) + 0.5, 0, 255).astype(np.uint8))
        im = Image.fromarray(np.concatenate(rows, 0))
        im.resize((w * 3, im.height * 3), Image.NEAREST).save(args.preview)


if __name__ == "__main__":
    main()
