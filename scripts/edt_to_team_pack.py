#!/usr/bin/env python3
"""edt_to_team_pack.py -- a SWOS 2020 fun-cup database (.EDT) as an OpenSWOS team pack (SWOS United).

    scripts/edt_to_team_pack.py FUNCUP.EDT --number 210 [--name NAME] [--description TEXT] --out PACK.json
    scripts/edt_to_team_pack.py FUNCUP.EDT --number 210 --mod DIR [--mod-name "..."]

SWOS 2020's FUN CUP plays teams from a custom database, MP\\DB\\<name>.EDT (2 bytes of count, then
684-byte team records -- the game's own team file layout), described by the first line of
<name>.TXT beside it (getFunCupDescsWithMask, game.cpp). SWOS United plays them in OpenSWOS's
formats (docs/SWOS2020_MODS.md): a TEAM PACK, the database's team JSON with uuids and checksums
(docs/TEAM_IDS.md §6), carried by a "data" mod. The .EDT is only read.

This reads the .EDT (scripts/swos_team_file.py, the decoder the database's JSON came from), writes
the pack with "fun_cup": true and the description, and hands it to OpenSWOS's
scripts/make_team_pack.py for its number, the new uuids, the checksums and PACK.registry.inc (the
matchmaking server's lines for its teams). With --mod it lays out a whole DLC mod folder:
DIR/mod.json (a new uuid4, "changes": "data") and DIR/data_json/teampacks/<name>.json, with the
server's lines beside the folder (DIR.registry.inc), not in it.
"""
import argparse
import json
import os
import subprocess
import sys
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swos_team_file as tf  # noqa: E402


def description_beside(edt):
    """The first line of the .TXT beside the .EDT (SWOS 2020's description), or ''."""
    base = os.path.splitext(edt)[0]
    for ext in (".TXT", ".txt"):
        try:
            with open(base + ext, encoding="latin-1") as f:
                return f.readline().strip().upper()
        except OSError:
            pass
    return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("edt", help="the SWOS 2020 fun-cup database (.EDT)")
    ap.add_argument("--number", type=int, required=True, help="the pack's number, 210..249")
    ap.add_argument("--name", help="the pack's name (default: the .EDT's file name)")
    ap.add_argument("--description", help="one line (default: the .TXT beside the .EDT)")
    out = ap.add_mutually_exclusive_group(required=True)
    out.add_argument("--out", help="the pack file to write")
    out.add_argument("--mod", help="a mod folder to lay out (mod.json + data_json/teampacks/)")
    ap.add_argument("--mod-name", help="with --mod: the mod's name (default: FUN CUP: <name>)")
    args = ap.parse_args()

    with open(args.edt, "rb") as f:
        blob = f.read()
    teams = tf.parse_blob(blob)
    if not teams or len(blob) < 2 + len(teams) * tf.TEAM_REC_SIZE:
        sys.exit("edt_to_team_pack: %s is not a team database (count %d, %d bytes)"
                 % (args.edt, len(teams), len(blob)))
    stem = os.path.splitext(os.path.basename(args.edt))[0]
    name = (args.name or stem).upper()
    pack = {
        "type": "team",
        "country": args.number,
        "country_name": name,
        "fun_cup": True,
        "description": (args.description if args.description is not None
                        else description_beside(args.edt)).upper(),
        "source": {"format": "swos2020-edt", "file": os.path.basename(args.edt)},
        "count": len(teams),
        "teams": teams,
    }

    if args.mod:
        slug = "funcup-" + "".join(c if c.isalnum() else "-" for c in stem.lower()).strip("-")
        path = os.path.join(args.mod, "data_json", "teampacks", stem.lower() + ".json")
        manifest = {
            "uuid": str(uuid.uuid4()),
            "id": slug,
            "name": args.mod_name or ("FUN CUP: " + name),
            "version": "1.0",
            "author": "SWOS 2020 fun cup (converted by SWOS United's scripts/edt_to_team_pack.py)",
            "about": pack["description"] or ("SWOS 2020's fun cup teams: " + name),
            "targets": ["sdl"],
            "changes": "data",
            "default": False,
        }
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(os.path.join(args.mod, "mod.json"), "w", newline="\n") as f:
            json.dump(manifest, f, indent=1)
            f.write("\n")
    else:
        path = args.out
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)

    with open(path, "w", newline="\n") as f:
        json.dump(pack, f, indent=1)
        f.write("\n")
    # Numbers, uuids, checksums, the server's registry lines: OpenSWOS's tool, so a pack made
    # here is made exactly as its own are.
    subprocess.run([sys.executable, os.path.join(HERE, "make_team_pack.py"), path,
                    "--number", str(args.number)], check=True)
    if args.mod:
        # The server's lines are for United's matchmaking server, not for players: beside the
        # mod folder, never in what the mod ships.
        reg = os.path.splitext(path)[0] + ".registry.inc"
        dest = os.path.normpath(args.mod) + ".registry.inc"
        os.replace(reg, dest)
        print("edt_to_team_pack: the server's lines -> %s" % dest)
    print("edt_to_team_pack: %d teams from %s -> %s" % (len(teams), args.edt, path))


if __name__ == "__main__":
    main()
