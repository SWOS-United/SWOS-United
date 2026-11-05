#!/usr/bin/env python3
"""Build SWOS United's menus2020 mod's menus from SWOS 2020's .mnu files.

SWOS United only (docs/SWOS2020_MODS.md, "menus2020"). SWOS 2020's main menu and the screens
its buttons open, imported through scripts/import_swos2020_menus.py's own classes (so the
DSL is read exactly as the SWOS 2020 build reads it), then changed for this game:

  * every handler is the mod's own, "menus2020.<name>" (src/mod_menus2020.c), resolved only
    while the mod is on (include/mod_api.h, ModApi_RegisterHandlers). A SWOS 2020 handler
    with no counterpart here is dropped and its row hidden (`HIDE` below);
  * the main menu replaces the game's (data_json/menus/swosMainMenu.json, through the mod
    overlay), so its entries are put in an order where 11..16 mean what they mean in the
    game's own main menu -- CONTINUE, SAVE, LOAD, MULTIPLAYER, CREDITS, MODES -- because
    CommonMenuExit (src/menus.c), which every main-menu handler ends in, sets those by
    ordinal. Navigation ordinals are renumbered to match.

Run from the repo root (needs a swos-2020 checkout: --swos2020 or $SWOS2020_DIR):
  python3 scripts/make_menus2020.py
Writes bin/mods/menus2020/data_json/menus/swosMainMenu.json and menus2020/<menu>.json.
"""
import argparse
import importlib.util
import json
import os
import subprocess

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "bin", "mods", "menus2020", "data_json", "menus")
SLUG = "menus2020"

spec = importlib.util.spec_from_file_location(
    "import_swos2020_menus", os.path.join(REPO, "scripts", "import_swos2020_menus.py"))
imp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(imp)


def mod(name):
    return "%s.%s" % (SLUG, name)


# Per menu: the .mnu stem, the name it is written under, its header handlers, SWOS 2020
# handler -> ours, and the entries hidden (no counterpart yet: SWOS 2020 features that come
# with other mods -- speedrun, challenges, practice, ticker -- or not at all).
SUBMENUS = {
    "competition": dict(
        out="competitionMenu", onInit="competitionInit",
        handlers={
            "selectOnline": "online",
            "selectDIYCompetitionMenu": "diy",
            "selectPresetCompetitionMenu": "preset",
            "SWOS_careerMainMenu_onSelect": "career",
            "SWOS_speedrunMainMenu_onSelect": "speedruns",  # OpenSWOS's speedrun mod, when on
            "selectScenarioMode": "challenges",             # its challenges mod, when on
        },
        hide=[],
        # SEASON: the original main menu's, which SWOS 2020 has not -- kept (the owner: every
        # feature of both), beside CLASSIC CAREER MODE in its row.
        move={"act3": (162, 137, 110, 20)},
        add=[dict(name="season", like="act3", coords=(32, 137, 110, 20),
                  symbol="_aSeason_1", handler="season")],
    ),
    "practice": dict(
        out="practiceMenu", onInit="practiceInit",
        handlers={
            "FriendlyMenu": "friendly",
            "trainingModePractice_OnSelect": "modes",   # this row is MODES (mod_menus2020.c)
            # OpenSWOS's practice mod's own handlers (its H10 names), resolved while it is on;
            # mod_menus2020.c hides the rows while it is off. They run nested in this screen,
            # which comes back from these bytes without its onInit: so the defaults here are
            # the practice-on layout.
            "challengeGame2Practice_OnSelect": "practice.shootout",
            "demoGamePractice_OnSelect": "practice.demo",
            "exitPractice_OnSelect": "SetExitMenuFlag",
        },
        hide=["HelpMsg", "trainingMode"],
        text={"trainingMode": "MODES"},
        nav={"friendlyMode": (-1, -1, -1, 3), "challengeGame2": (-1, -1, 1, 4),
             "demoGame": (-1, -1, 3, 6), "exit": (-1, -1, 4, -1)},
    ),
    "editing": dict(
        out="editingMenu", onInit=None,
        handlers={
            "EditCustomTeamMenu": "editTeams",
            "EditTacticsMenu": "editTactics",
            "exit_editingMenu_onSelect": "SetExitMenuFlag",
        },
        hide=[],
        # SAVE DISK FILING: the original main menu's (SWOS 2020 has none), under the editors.
        move={"editCustomTeams": (92, 50, 125, 20), "editTactics": (92, 95, 125, 20)},
        add=[dict(name="diskFiling", like="editTactics", coords=(92, 140, 125, 20),
                  symbol="_aSaveDiskFiling", handler="diskFiling")],
        nav={"editTactics": (-1, -1, 1, 4), "diskFiling": (-1, -1, 2, 3), "exit": (-1, -1, 4, -1)},
    ),
    "extrasEx": dict(
        out="extrasMenu", onInit="extrasInit",
        handlers={
            "EditCustomTeamMenu": "editTeams",
            "EditTacticsMenu": "editTactics",
            "manualExtras_OnSelect": "manual",
            "exitExtrasEx_OnSelect": "SetExitMenuFlag",
        },
        hide=["newsList", "tipsList", "streamsList", "HelpMsg"],   # the ticker mod's
    ),
}

# The main menu: SWOS 2020's entries by name, in the order whose ordinals 11..16 are the
# game's own main menu's (CommonMenuExit). None = an invisible filler.
MAIN_ORDER = ["competition", "practice", "replays", "options", "editing", "extras",
              "exit", None, None, None, None,
              "continueContest", "saveContest", "loadOldCompetition",
              "multiplayer", "credits", "modes"]
MAIN_HANDLERS = {
    "SWOS_competitionMainMenu_onSelect": "compete",
    "SWOS_practiceMainMenu_onSelect": "practice",
    "showReplaysMenu": "replays",
    "SWOS_OptionsMenuSelected": "options",
    "SWOS_editingMainMenu_onSelect": "editing",
    "SWOS_extrasMainMenu_onSelect": "extras",
    "SWOS_selectContinueContest": "continueContest",
    "SWOS_selectSaveContest": "saveContest",
    "SWOS_selectLoadOldCompetitionMenu": "loadContest",
    "showQuitMenu": "quit",
    "SWOS_creditsMainMenu_onSelect": "credits",
}


def entries_of(doc):
    """[(item, origin)] for every entry, with the origin it is drawn from."""
    out, origin = [], (0, 0)
    for it in doc["items"]:
        if it["kind"] == "set_origin":
            origin = (it["x"], it["y"])
        elif it["kind"] == "entry":
            out.append((it, origin))
        else:
            raise SystemExit("make_menus2020: %s: unexpected item %r" % (doc["name"], it))
    return out


def rehandle(item, handlers, where):
    """SWOS 2020's handler names -> the mod's; one with no counterpart is dropped."""
    cmds = []
    for c in item["commands"]:
        f = c.get("func")
        if f is None or f == "SetExitMenuFlag":
            cmds.append(c)
        elif f in handlers:
            ours = handlers[f]          # ours, or another mod's "<slug>.<name>" as it is
            cmds.append(dict(c, func=ours if ours == "SetExitMenuFlag" or "." in ours else mod(ours)))
        else:
            print("  %s: %s dropped (no counterpart)" % (where, f))
    item["commands"] = cmds


def hide(item):
    if not any(c["op"] == "invisible" for c in item["commands"]):
        item["commands"].append({"op": "invisible"})


def set_text(item, text=None, symbol=None, deferred=False):
    """Replace (or add) the entry's text: a literal, a SWOS string symbol, or a buffer the
    game fills in (text_no_ptr)."""
    flags = "0x0"
    rest = []
    for c in item["commands"]:
        if c["op"] in ("text", "text_no_ptr"):
            flags = c.get("flags", flags)
        else:
            rest.append(c)
    if text is not None or symbol:
        flags = "0x%X" % (int(flags, 16) & ~0xC000)     # centred (kLeftAligned / kRightAligned off)
    if deferred:
        cmd = {"op": "text_no_ptr", "flags": flags}
    elif symbol:
        cmd = {"op": "text", "flags": flags, "str": symbol}
    else:
        cmd = {"op": "text", "flags": flags, "literal": text}
    item["commands"] = [cmd] + rest


def renumber(item, remap):
    for c in item["commands"]:
        if c["op"] == "nav":
            for d in ("left", "right", "up", "down"):
                if c[d] != -1:
                    c[d] = remap.get(c[d], -1)
        elif c["op"].endswith("_skip") and c.get("skip", -1) != -1:
            c["skip"] = remap.get(c["skip"], -1)


def emit(items_with_origin):
    """Items back from [(item, origin)], with a set_origin wherever it changes."""
    items, cur = [], (0, 0)
    for it, origin in items_with_origin:
        if origin != cur:
            items.append({"kind": "set_origin", "x": origin[0], "y": origin[1]})
            cur = origin
        items.append(it)
    return items


def colors_of(item):
    return next((c for c in item["commands"] if c["op"] == "colors"), None)


def build_main(doc):
    found = entries_of(doc)
    by_name = {it.get("name"): (it, o) for it, o in found}
    old_ord = {it.get("name"): i for i, (it, _o) in enumerate(found)}

    for it, _o in found:
        rehandle(it, MAIN_HANDLERS, "mainMenu." + str(it.get("name")))

    button = by_name["saveContest"]
    base_colors = colors_of(button[0])

    # 11: the game prints "CONTINUE <name>" / "REPLAY <name>" into it (CommonMenuExit).
    set_text(by_name["continueContest"][0], deferred=True)
    set_text(by_name["saveContest"][0], "SAVE")
    set_text(by_name["loadOldCompetition"][0], "LOAD")
    # The two small icons SWOS 2020 draws in its onDraw (not ported): words instead.
    quit_item, quit_origin = by_name["exit"]
    quit_item["coords"] = {"x": 4, "y": 182, "w": 56, "h": 18}
    set_text(quit_item, "QUIT")
    credits_item, credits_origin = by_name["credits"]
    credits_item["coords"] = {"x": 244, "y": 182, "w": 56, "h": 18}
    set_text(credits_item, symbol="_aCredits")
    for it in (quit_item, credits_item):
        if base_colors and not colors_of(it):
            it["commands"].insert(1, dict(base_colors))

    # 14 and 16, the game's own MULTIPLAYER and MODES: this menu reaches online play through
    # COMPETE and the modes through PRACTICE, so both stay hidden (mod_menus2020.c keeps
    # them so after CommonMenuExit, which shows MODES when a mod has an entry).
    multiplayer = {"kind": "entry", "name": "multiplayer",
                   "coords": dict(credits_item["coords"]),
                   "commands": [{"op": "text", "flags": "0x0", "str": "_aMultiplayer"},
                                {"op": "onselect_fire", "func": mod("compete")},
                                {"op": "invisible"}]}
    modes = {"kind": "entry", "name": "modes",
             "coords": dict(credits_item["coords"]),
             "commands": [{"op": "text", "flags": "0x0", "literal": "MODES"},
                          {"op": "onselect_fire", "func": mod("practice")},
                          {"op": "invisible"}]}
    extra = {"multiplayer": (multiplayer, credits_origin), "modes": (modes, credits_origin)}

    remap = {}
    for new, name in enumerate(MAIN_ORDER):
        if name in old_ord:
            remap[old_ord[name]] = new

    ordered = []
    for name in MAIN_ORDER:
        if name is None:
            ordered.append(({"kind": "entry", "coords": {"x": 0, "y": 0, "w": 1, "h": 1},
                             "commands": [{"op": "invisible"}]}, (0, 0)))
        elif name in extra:
            ordered.append(extra[name])
        else:
            it, o = by_name[name]
            renumber(it, remap)
            ordered.append((it, o))
    dropped = [n for n in old_ord if n not in MAIN_ORDER]
    if dropped:
        print("  mainMenu: dropped %s (SWOS 2020's TV icon and animation, not ported)" % dropped)

    doc["items"] = emit(ordered)
    doc["name"] = "swosMainMenu"
    doc["onInit"] = mod("mainInit")
    doc["onDraw"] = None
    doc["onReturn"] = None
    doc["defaultEntry"] = 0
    return doc


def build_sub(doc, spec):
    found = entries_of(doc)
    by_name = {it.get("name"): it for it, _o in found}
    for it, _o in found:
        name = it.get("name")
        rehandle(it, spec["handlers"], "%s.%s" % (spec["out"], name))
        if name in spec["hide"]:
            hide(it)
    for name, text in spec.get("text", {}).items():
        set_text(by_name[name], text)
    for name, (x, y, w, h) in spec.get("move", {}).items():
        by_name[name]["coords"] = {"x": x, "y": y, "w": w, "h": h}
    # Entries of ours, after SWOS 2020's (so its ordinals stay): a copy of `like` (its colours,
    # text flags) with our text and handler.
    for add in spec.get("add", []):
        item = json.loads(json.dumps(by_name[add["like"]]))
        item["name"] = add["name"]
        x, y, w, h = add["coords"]
        item["coords"] = {"x": x, "y": y, "w": w, "h": h}
        item["commands"] = [c for c in item["commands"] if c["op"] not in ("nav", "onselect_fire")]
        set_text(item, symbol=add["symbol"])
        item["commands"].append({"op": "onselect_fire", "func": mod(add["handler"])})
        doc["items"].append(item)
        by_name[add["name"]] = item
    for name, (l, r, u, d) in spec.get("nav", {}).items():
        it = by_name[name]
        it["commands"] = [c for c in it["commands"] if c["op"] != "nav"]
        it["commands"].append({"op": "nav", "left": l, "right": r, "up": u, "down": d})
    doc["name"] = spec["out"]
    doc["onInit"] = mod(spec["onInit"]) if spec["onInit"] else None
    doc["onDraw"] = None
    doc["onReturn"] = None
    return doc


def write(path, doc):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    print("  -> %s" % os.path.relpath(path, REPO))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--swos2020", default=imp.DEFAULT_SWOS2020, help="swos-2020 checkout")
    args = ap.parse_args()
    root = os.path.abspath(args.swos2020)
    mnu2h = imp.Mnu2h(root)
    try:
        commit = subprocess.check_output(["git", "-C", root, "rev-parse", "--short", "HEAD"],
                                         text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        commit = "unknown"

    def load(stem):
        rel = "src/menus/%s.mnu" % stem
        menus = mnu2h.Parser.Parser(os.path.join(root, rel)).getMenus()
        (name, menu), = menus.items()
        doc, _entries = imp.MenuImporter(mnu2h, name, menu, rel, commit).build()
        return doc

    write(os.path.join(OUT, "swosMainMenu.json"), build_main(load("main")))
    for stem, spec in SUBMENUS.items():
        write(os.path.join(OUT, SLUG, spec["out"] + ".json"), build_sub(load(stem), spec))


if __name__ == "__main__":
    main()
