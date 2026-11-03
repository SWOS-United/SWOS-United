#!/usr/bin/env python3
"""gen_swos2020_lobby_art.py -- SWOS 2020's lobby art for SWOS United's lobby.

Writes include/swos2020_lobby_art.h from a SWOS 2020 checkout (github.com/starwindz/swos-2020):

  - the flag atlas, vc-proj/flags_atlas.bmp: 240x88, 24-bit, a 20x11 grid of 12x8 flags, kept
    as RGB here (the game maps it to the menu palette when it draws one);
  - its ISO2 -> flag table (game.cpp g_countryFlags) and the four home nations' own flags;
  - the country table (util.cpp country_names: name, ISO2, ISO3), for the old site's nation
    names;
  - starsMap (36x10 per level, 10 levels) and check_symbols_and_etc_12x13_4x1_map
    (PixelmapAndStrings.h): the PLAYERS column's stars and GO's tick / cross;
  - UniformSprite0-3 (PixelmapAndStrings.h): the little players in a team's kit, front and back.

Usage: scripts/gen_swos2020_lobby_art.py [path/to/swos-2020]   (default /mnt/c/Projects/swos-2020)
Commit the header it writes; no build runs this.
"""
import os
import re
import struct
import sys

src = sys.argv[1] if len(sys.argv) > 1 else '/mnt/c/Projects/swos-2020'
out = os.path.join(os.path.dirname(__file__), '..', 'include', 'swos2020_lobby_art.h')


def read(path):
    return open(os.path.join(src, path), encoding='latin-1').read()


def strings(block):
    return re.findall(r'"((?:[^"\\]|\\.)*)"', block)


# ---- the atlas ----
bmp = open(os.path.join(src, 'vc-proj', 'flags_atlas.bmp'), 'rb').read()
assert bmp[:2] == b'BM'
off = struct.unpack_from('<I', bmp, 10)[0]
w, h = struct.unpack_from('<ii', bmp, 18)
bpp = struct.unpack_from('<H', bmp, 28)[0]
assert (w, abs(h), bpp) == (240, 88, 24), (w, h, bpp)
stride = (w * 3 + 3) & ~3
rgb = bytearray()
for y in range(abs(h)):
    row = y if h < 0 else abs(h) - 1 - y
    p = off + row * stride
    for x in range(w):
        b, g, r = bmp[p + x * 3], bmp[p + x * 3 + 1], bmp[p + x * 3 + 2]
        rgb += bytes((r, g, b))

# ---- the tables ----
game = read('src/game.cpp')
flags = re.findall(r'\{"([A-Z]{2})",(\d+)\}', re.search(r'g_countryFlags\[240\] = \{(.*?)\};', game, re.S).group(1))
util = read('src/util/util.cpp')
names = strings(re.search(r'const char\* country_names\[\] = \{(.*?)\};', util, re.S).group(1))
assert len(names) % 3 == 0
pix = read('src/PixelmapAndStrings.h')
stars = strings(re.search(r'const char\* starsMap\[\] = \{(.*?)\};', pix, re.S).group(1))
check = strings(re.search(r'const char\* check_symbols_and_etc_12x13_4x1_map\[\] = \{(.*?)\};', pix, re.S).group(1))
assert len(stars) == 100 and len(check) == 13
uniforms = [strings(re.search(r'UniformSprite%d\[\] = \{(.*?)\};' % n, pix, re.S).group(1)) for n in range(4)]
assert all(len(u) == 14 and all(len(r) == 96 for r in u) for u in uniforms)

with open(out, 'w') as f:
    f.write('/* swos2020_lobby_art.h -- SWOS 2020\'s lobby art (github.com/starwindz/swos-2020), written by\n'
            ' * scripts/gen_swos2020_lobby_art.py: never edit by hand. The flag atlas (vc-proj/flags_atlas.bmp,\n'
            ' * 20x11 flags of 12x8, RGB), its ISO2 -> flag table, the country table, the stars and GO\'s\n'
            ' * symbols. */\n#ifndef SWOS2020_LOBBY_ART_H\n#define SWOS2020_LOBBY_ART_H\n\n#include <stdint.h>\n\n')
    f.write('enum { kFlagAtlasW = 240, kFlagAtlasH = 88, kFlagW = 12, kFlagH = 8, kFlagCols = 20 };\n\n')
    f.write('static const uint8_t k_flagAtlasRgb[%d] = {\n' % len(rgb))
    for i in range(0, len(rgb), 24):
        f.write('    ' + ','.join(str(v) for v in rgb[i:i + 24]) + ',\n')
    f.write('};\n\n')
    f.write('/* ISO2 -> flag (0: the atlas\'s "unknown"); the home nations by their site codes. */\n')
    f.write('static const struct { const char *code; int16_t flag; } k_countryFlags[] = {\n')
    for i in range(0, len(flags), 6):
        f.write('    ' + ' '.join('{ "%s", %s },' % c for c in flags[i:i + 6]) + '\n')
    f.write('    { "GB-ENG", 213 }, { "GB-SCT", 216 }, { "GB-WLS", 217 }, { "GB-NIR", 214 },\n};\n\n')
    f.write('/* name (capitals), ISO2, ISO3 -- util.cpp country_names */\n')
    f.write('static const char *const k_countryNames[][3] = {\n')
    for i in range(0, len(names), 3):
        f.write('    { "%s", "%s", "%s" },\n' % tuple(names[i:i + 3]))
    f.write('};\n\n')
    f.write('/* 10 star levels, 36x10 each, one under the other; \'0\' clear, else a palette index in hex */\n')
    f.write('static const char *const k_starsMap[100] = {\n')
    for r in stars:
        f.write('    "%s",\n' % r)
    f.write('};\n\n/* 12x13 symbols side by side: GO uses 2 (ready, a tick) and 3 (not, a cross) */\n')
    f.write('static const char *const k_checkSymbols[13] = {\n')
    for r in check:
        f.write('    "%s",\n' % r)
    f.write('};\n\n/* the lobby\'s little players (UniformSprite0-3, one a shirt type): 14 rows, 96 wide -- the back\n'
            ' * (0) then the front (1), three running frames of 16 each. \'0\'-\'3\' the shirt, its second colour,\n'
            ' * the shorts, the socks; a-h hair and skin; \'.\' and \'#\' clear (uniConvColor). */\n')
    f.write('static const char *const k_uniformSprite[4][14] = {\n')
    for u in uniforms:
        f.write('    {\n')
        for r in u:
            f.write('        "%s",\n' % r)
        f.write('    },\n')
    f.write('};\n\n#endif /* SWOS2020_LOBBY_ART_H */\n')
print('wrote', os.path.normpath(out), len(flags), 'flags,', len(names) // 3, 'countries')
