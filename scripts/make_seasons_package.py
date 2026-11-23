#!/usr/bin/env python3
"""make_seasons_package.py -- the classic seasons for CLASSIC CAREER' SEASON START, in one package.

    python3 -I scripts/make_seasons_package.py [--mods DIR] [--out FILE]

SWOS United plays the seasons of SWOS 2020's team database DLC (docs/MODS.md §10.4) from a folder of
their own beside the game, seasons/<slug>/ (include/mods.h MODS_SEASONS_DIR): one is on at a time,
the one SEASON START chose ([general] season in SWOS.ini), and only while 96/97 is the world played.
This takes the team database mods tools/dlc-manager/scripts/dlc2020_to_mods.py made
(--types teamdb --openswos .: DIR/td-*.zip, default dist/seasons/teamdb) and writes them as one zip,
seasons/<slug>/..., each mod.json given the label SEASON START shows ("season_label") and its place
in the list ("season_sort"):

  - every season of the catalogue but the SPECIAL EDITIONs (not seasons: a World Cup, legends,
    fantasy teams), and "1996_97 - Original Amiga _ PC DOS" (the game's own 96/97, SEASON START's
    first choice);
  - the game's own 96/97's readme.txt as seasons/ORIGINAL-9697.TXT (SEASON START's INFO: each
    season's own readme.txt is in its folder);
  - YYYY/YY when the season has one database; with several, the Amiga's releases by their version
    (1994/95 v0.9, v1.0, v1.1), the others by a short word of their title (1996/97 REVISED,
    2019/20 WINTER ...) -- the season's main one (Team Updates, SWOS 2020, PC DOS) plain.

The installer (scripts/build_installer.sh) unpacks the zip into the game's folder; other platforms
take the same folder. The teams are their authors' (each mod's "author" and readme.txt): the
package is never committed.
"""
import argparse
import json
import os
import re
import sys
import zipfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The game's own 96/97 (SEASON START's "1996/97"), and the packs that are not seasons.
SKIP_TITLES = {"1996_97 - Original Amiga _ PC DOS"}
SKIP_PREFIX = "SPECIAL EDITION"
# A season with several databases: the word each other one is known by (the main one has none).
TAGS = [("Winter Transfer", "WINTER"), ("Revised", "REVISED"), ("European Championship", "EURO 96"),
        ("Synchronated", "SYNC"), ("Taraji", "TARAJI"), ("Reserve", "RESERVES")]


def season_of(title):
    m = re.match(r"(\d{4})_(\d{2}) - (.*)$", title)
    return (m.group(1), m.group(2), m.group(3)) if m else None


def labels(titles):
    """{title: (label, sort key)} for the seasons' titles."""
    by_season = {}
    for t in titles:
        y1, y2, rest = season_of(t)
        by_season.setdefault((y1, y2), []).append((t, rest))
    out = {}
    for (y1, y2), packs in by_season.items():
        base = "%s/%s" % (y1, y2)
        for t, rest in packs:
            amiga = re.search(r"Original Amiga (\d+\.\d+)", rest)
            if len(packs) == 1:
                label, order = base, 0
            elif amiga:
                label, order = "%s v%s" % (base, amiga.group(1)), int(float(amiga.group(1)) * 10)
            else:
                tag = next((w for k, w in TAGS if k.lower() in rest.lower()), None)
                # (a season's two packs where neither has a word: the second by its title's start)
                label = "%s %s" % (base, tag) if tag else base
                order = 50 if tag else 0
            out[t] = (label, "%s-%02d" % (y1, order))
    seen = {}
    for t, (label, _s) in sorted(out.items()):
        if label in seen:
            sys.exit("make_seasons_package: %r and %r would both be %s -- give one a word in TAGS"
                     % (seen[label], t, label))
        seen[label] = t
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mods", default=os.path.join(REPO, "dist", "seasons", "teamdb"))
    ap.add_argument("--out", default=os.path.join(REPO, "dist", "SWOS-United-Seasons.zip"))
    a = ap.parse_args()

    packs, original = [], None
    for name in sorted(os.listdir(a.mods)):
        if not (name.startswith("td-") and name.endswith(".zip")):
            continue
        z = zipfile.ZipFile(os.path.join(a.mods, name))
        manifest = json.loads(z.read("mod.json"))
        title = manifest.get("title", "")
        if title in SKIP_TITLES and "readme.txt" in z.namelist():
            original = z.read("readme.txt")     # (96/97's own notes: SEASON START's INFO)
        if title.startswith(SKIP_PREFIX) or title in SKIP_TITLES or not season_of(title):
            continue
        packs.append((title, name, z, manifest))
    if not packs:
        sys.exit("make_seasons_package: no team database mods in %s (dlc2020_to_mods.py --types teamdb first)" % a.mods)
    # (the game's own 96/97 counted as its season's main database: the revised one is "REVISED")
    named = labels([p[0] for p in packs] + sorted(SKIP_TITLES))

    tmp = a.out + ".part"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for title, name, z, manifest in sorted(packs, key=lambda p: named[p[0]][1]):
            slug = manifest["id"]
            manifest["season_label"], manifest["season_sort"] = named[title]
            for info in z.infolist():
                if info.is_dir():
                    continue
                data = z.read(info.filename)
                if info.filename == "mod.json":
                    data = (json.dumps(manifest, indent=1, ensure_ascii=False) + "\n").encode("utf-8")
                out.writestr("seasons/%s/%s" % (slug, info.filename), data)
            print("  %-16s %s" % (named[title][0], title))
        if original:                                # (src/career_seasons.c ORIGINAL_README)
            out.writestr("seasons/ORIGINAL-9697.TXT", original)
    os.replace(tmp, a.out)
    print("%d seasons -> %s (%.1f MB)" % (len(packs), os.path.relpath(a.out, REPO), os.path.getsize(a.out) / 1048576))


if __name__ == "__main__":
    main()
