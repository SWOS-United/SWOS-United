#!/usr/bin/env python3
"""nat_ranking.py -- World 2026-27's national ranking and confederations (docs/WORLD_DATAGEN.md §18).

    python3 -I scripts/worldgen/nat_ranking.py --csv TEAMS.csv --ranking RANKING.html [--world DIR] [--banned RUS,...]

Reads the owner's list of national teams (CSV: National Team, Confederation, FIFA Member) and FIFA's
men's ranking page as the owner's browser saved it (never fetched), matches both to the world's
national team files (teams/EUROPE.json ... OCEANIA.json, countries 80..85), and writes
competitions/national_ranking.json in the world folder (default bin/data_json/worlds/2627): per team
its country and ordinal, FIFA code, ranking points (hundredths), position, confederation and whether
it is banned from every competition. The game reads it at start-up (DataJson_LoadNationalRanking,
src/save_format_json.c) as the base a career's ranking starts from (src/nat_rank.c).

Every one of the world's national teams must match; a name the two sources spell differently is
listed in ALIASES (FIFA's name -> the CSV's) or GAME_NAMES (FIFA's name -> the game's). A team that
matches nothing stops the script, naming it."""

import argparse
import csv
import html
import json
import os
import re
import sys
import unicodedata

CONFEDERATIONS = ["UEFA", "CAF", "CONMEBOL", "CONCACAF", "AFC", "OFC"]     # 0..5, as files 80..85
FILES = [("EUROPE", 80), ("AFRICA", 81), ("SOUTH_AMERICA", 82), ("NORTH_AMERICA", 83), ("ASIA", 84), ("OCEANIA", 85)]

# FIFA's spelling -> the owner's CSV's.
ALIASES = {
    "Congo DR": "DR Congo", "Côte d'Ivoire": "Ivory Coast", "DPR Korea": "North Korea", "IR Iran": "Iran",
    "Korea Republic": "South Korea", "Kyrgyz Republic": "Kyrgyzstan", "Macau, China": "Macau",
    "St. Kitts and Nevis": "Saint Kitts and Nevis", "St. Lucia": "Saint Lucia",
    "St. Vincent / Grenadines": "Saint Vincent and the Grenadines", "The Gambia": "Gambia", "USA": "United States",
}
# FIFA's spelling -> the game's team name, where normalising does not find it.
GAME_NAMES = {
    "Netherlands": "HOLLAND", "Türkiye": "TURKEY", "Republic of Ireland": "REPUBLIC IRELAND",
    "Bosnia and Herzegovina": "BOSNIA-HERZ.", "United Arab Emirates": "UNITED ARAB EMIS", "Cabo Verde": "CAPE VERDE",
    "China PR": "CHINA", "Trinidad and Tobago": "TRINIDAD-TOBAGO", "Equatorial Guinea": "EQUAT. GUINEA",
    "Faroe Islands": "FAROE ISLES", "Suriname": "SURINAM", "Dominican Republic": "DOMINICAN REP.",
    "Central African Republic": "CENTRAL AFRICA", "St. Kitts and Nevis": "ST. KITTS-NEVIS",
    "Antigua and Barbuda": "ANTIGUA-BARBUDA", "St. Vincent / Grenadines": "ST. VINCENT", "Chinese Taipei": "TAIWAN",
    "São Tomé and Príncipe": "SAO TOME", "Turks and Caicos Islands": "TURKS-CAICOS", "US Virgin Islands": "US VIRGIN IS.",
    "British Virgin Islands": "BRIT. VIRGIN IS.",
}


def norm(s):
    s = unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().upper()
    return re.sub(r"[^A-Z]", "", s)


def read_ranking(path):
    """FIFA's table as saved: (position, name, code, points) per row."""
    with open(path, encoding="utf-8", errors="replace") as f:
        s = f.read()
    rows = []
    for tr in re.findall(r"<tr[^>]*>(.*?)</tr>", s, re.S):
        name = re.search(r'alt="([^"]+)" class="custom-team-cell_teamFlag', tr)
        if not name:
            continue
        code = re.search(r"world-ranking/([A-Z]{3})\?", tr)
        pos = re.search(r"<h3[^>]*>(\d+)</h3>", tr)
        pts = re.findall(r">(\d{2,4}\.\d{1,2})<", tr)
        if not (code and pos and pts):
            sys.exit("nat_ranking: a ranking row without code, position or points: %s" % html.unescape(name.group(1)))
        rows.append((int(pos.group(1)), html.unescape(name.group(1)), code.group(1), float(pts[0])))
    date = re.search(r'"lastUpdateDate":"(\d{4}-\d{2}-\d{2})', s)
    return rows, (date.group(1) if date else "")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--csv", required=True, help="the owner's national teams CSV")
    ap.add_argument("--ranking", required=True, help="FIFA's men's ranking page, saved")
    ap.add_argument("--world", default="bin/data_json/worlds/2627", help="the world's folder")
    ap.add_argument("--banned", default="RUS", help="FIFA codes banned from every competition (comma separated)")
    a = ap.parse_args()

    with open(a.csv, encoding="utf-8-sig") as f:
        conf = {r["National Team"]: r["Confederation"] for r in csv.DictReader(f) if r["FIFA Member"].strip() == "Yes"}
    game = {}
    for fname, country in FILES:
        with open(os.path.join(a.world, "teams", fname + ".json"), encoding="utf-8") as f:
            for t in json.load(f)["teams"]:
                game[norm(t["team"]["team_name"])] = (country, t["team"]["team_ordinal"], t["team"]["team_name"])
    rows, date = read_ranking(a.ranking)
    banned = {c.strip() for c in a.banned.split(",") if c.strip()}

    out, seen, missing = [], set(), []
    for pos, name, code, pts in rows:
        cname = ALIASES.get(name, name)
        if cname not in conf:
            missing.append("%s: not in the CSV" % name)
            continue
        g = game.get(norm(GAME_NAMES.get(name, cname))) or game.get(norm(name))
        if not g:
            missing.append("%s: no game team" % name)
            continue
        seen.add((g[0], g[1]))
        out.append({"country": g[0], "ordinal": g[1], "name": g[2], "code": code, "rank": pos,
                    "points": int(round(pts * 100)), "confederation": CONFEDERATIONS.index(conf[cname]),
                    "banned": code in banned})
    unmatched = [n for k, (c, o, n) in game.items() if (c, o) not in seen]
    if missing or unmatched:
        sys.exit("nat_ranking: unmatched -- ranking: %s; game: %s" % (missing, unmatched))

    dest = os.path.join(a.world, "competitions", "national_ranking.json")
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    doc = {"about": "World 2026-27's national ranking (FIFA, %s) and confederations: scripts/worldgen/nat_ranking.py"
                    % date, "date": date, "confederations": CONFEDERATIONS, "teams": out}
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1, ensure_ascii=False)
        f.write("\n")
    per = [sum(1 for t in out if t["confederation"] == i) for i in range(6)]
    print("nat_ranking: %d teams (%s), %d banned -> %s" % (len(out), ", ".join(
        "%s %d" % (CONFEDERATIONS[i], per[i]) for i in range(6)), sum(t["banned"] for t in out), dest))


if __name__ == "__main__":
    main()
