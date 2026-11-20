#!/usr/bin/env bash
# make_loader_logos.sh -- SWOS United's loader pictures (Playa, 2026-11-20), in place of the game's:
#   bin/grafs_aga/SWOSUNTD.RAW  the title picture (loader1.raw's place, src/loader_intro.c TITLE_PICTURE):
#                               packaging/intro/swos-united-logo.png in loader1's logo area (257x213)
#   bin/grafs_aga/POWERED.RAW   the disk pictures (olddisk1/disk22's place, src/SWOS.c): POWERED BY in
#                               the menu font over packaging/intro/openswos-logo.png, the disk's height
# Both are the project's own and tracked; an OCS build keeps the game's. Needs tools/swosdata built
# (tools/swosdata/build.sh: libswosdata.a) and bin/grafs_aga/CHARSET.RAW (the setup's) for the font.
# Previews go to build/loader_logos/.
set -euo pipefail
cd "$(dirname "$0")/.."
LIB=tools/swosdata/build/libswosdata.a
[ -f "$LIB" ] || { echo "make_loader_logos: build tools/swosdata first ($LIB)" >&2; exit 1; }
[ -f bin/grafs_aga/CHARSET.RAW ] || { echo "make_loader_logos: no bin/grafs_aga/CHARSET.RAW (run the data setup)" >&2; exit 1; }
OUT=build/loader_logos
mkdir -p "$OUT"
gcc -O2 -o "$OUT/make_loader_logos" scripts/make_loader_logos.c -Ithird_party/swosgfx/lib -Ithird_party/stb_image \
    -Iinclude "$LIB" -lm
"$OUT/make_loader_logos" packaging/intro/swos-united-logo.png bin/grafs_aga/SWOSUNTD.RAW "$OUT/swosuntd.png" \
    176 127 257 213 0
"$OUT/make_loader_logos" packaging/intro/openswos-logo.png bin/grafs_aga/POWERED.RAW "$OUT/powered.png" \
    175 150 200 124 1 "POWERED BY" bin/grafs_aga/CHARSET.RAW
