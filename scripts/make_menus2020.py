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
        onDraw="competitionDraw",       # (competitionMenu_OnDraw: the arrows either side of CHALLENGES)
        handlers={
            "selectOnline": "online",
            "selectDIYCompetitionMenu": "diy",
            "selectPresetCompetitionMenu": "preset",
            "SWOS_careerMainMenu_onSelect": "career",
            "SWOS_speedrunMainMenu_onSelect": "speedruns",  # OpenSWOS's speedrun mod, when on
            "selectScenarioMode": "challenges",             # its challenges mod, when on
        },
        hide=[],
        # (SEASON, the original main menu's, was kept beside CLASSIC CAREER MODE until
        # 2026-11-10: not needed any more, the owner -- the row is SWOS 2020's again.)
    ),
    "practice": dict(
        out="practiceMenu", onInit="practiceInit",
        handlers={
            "FriendlyMenu": "friendly",
            # OpenSWOS's practice mod's own handlers (its H10 names), resolved while it is on;
            # mod_menus2020.c hides the rows while it is off. They run nested in this screen,
            # which comes back from these bytes without its onInit: so the defaults here are
            # the practice-on layout.
            "trainingModePractice_OnSelect": "practice.training",
            "challengeGame2Practice_OnSelect": "practice.shootout",
            "demoGamePractice_OnSelect": "practice.demo",
            "exitPractice_OnSelect": "SetExitMenuFlag",
        },
        hide=["HelpMsg"],
        # MODES (the mods' entries, ours): under DEMO GAME, where SWOS 2020's help line was;
        # hidden by default, shown by mod_menus2020.c when a mod other than practice has one.
        add=[dict(name="modes", like="demoGame", coords=(90, 162, 124, 18),
                  literal="MODES", handler="modes", hidden=True)],
        nav={"friendlyMode": (-1, -1, -1, 2), "trainingMode": (-1, -1, 1, 3),
             "challengeGame2": (-1, -1, 2, 4), "demoGame": (-1, -1, 3, 6),
             "exit": (-1, -1, 4, -1)},
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
        out="extrasMenu", onInit="extrasInit", onDraw="extrasDraw",
        handlers={
            "EditCustomTeamMenu": "editTeams",
            "EditTacticsMenu": "editTactics",
            "manualExtras_OnSelect": "manual",
            "tipsListExtras_OnSelect": "tips",
            "streamsListExtras_OnSelect": "streams",
            "exitExtrasEx_OnSelect": "SetExitMenuFlag",
        },
        # LATEST NEWS: SWOS 2020's news.txt, which its launcher fetched -- no source here yet
        # (the owner, 2026-11-10: left out for now). TIPS AND REVIEWS and STREAMS AND VIDEOS
        # list the ticker's news (tips.txt), in SWOS 2020's ticker.mnu (below).
        hide=["newsList"],
        nav={"tipsList": (-1, 5, -1, 3), "streamsList": (-1, 6, 2, 8),
             "editCustomTeams": (2, -1, -1, 5), "editTactics": (2, -1, 4, 6),
             "manual": (3, -1, 5, 8), "exit": (-1, -1, 6, -1)},
    ),
    # REPLAYS (replaysMain.mnu, replaysMenu.cpp): SWOS GALORE -- the galore2020 mod's screen, what
    # PRACTICE's MODES opened until 2026-11-15 --, HIGHLIGHTS (the game's own), MATCH REPLAYS (the
    # game's highlights screen with the last match's whole replay, LATEST.SRP, loaded: SWOS 2020's
    # full-length match replays) and REPLAY CONTROLS; the line under them says what the chosen one
    # does (replaysMainMenu_OnDraw).
    "replaysMain": dict(
        out="replaysMenu", onInit="replaysInit", onDraw="replaysDraw",
        handlers={
            "swosGalore_OnSelect": "galore",
            "highlights_OnSelect": "highlights",
            "matchReplays_OnSelect": "matchReplays",
            "replayControls_OnSelect": "replayControls",
        },
        hide=[],
        deferred=["desc"],
    ),
    # REPLAY CONTROLS (replayControls.mnu): the keys and the controller's buttons, the picture its
    # onDraw draws (drawReplayControlsLayout) above them.
    "replayControls": dict(
        out="replayControlsMenu", onInit=None, onDraw="replayControlsDraw",
        handlers={},
        hide=[],
    ),
    # QUIT's screen (menu.cpp showQuitMenu): QUIT TO <the system> / RETURN TO GAME, RETURN chosen;
    # fading out and in as its quitMenuOnInit / quitGame / returnToGame.
    "quit": dict(
        out="quitMenu", onInit="quitInit",
        handlers={"quitGame": "quitGame", "returnToGame": "returnToGame"},
        hide=[],
        text={"quit": "QUIT", "returnToGame": "RETURN TO GAME"},   # (QUIT TO ...: quitInit)
    ),
    # The list TIPS AND REVIEWS and STREAMS AND VIDEOS open (game.cpp's tickerMenu_*): ten rows
    # a page -- number, category, text -- a row opening its link.
    "ticker": dict(
        out="tickerMenu", onInit="tickerInit", onDraw="tickerDraw",
        handlers={
            "ticker_OnSelect": "tickerOpen",
            "tickerScrollUp_onSelect": "tickerUp",
            "tickerScrollDown_onSelect": "tickerDown",
            "tickerMenuExit_OnSelect": "SetExitMenuFlag",
        },
        hide=[],
        # The page buttons (ticker.mnu: 303,48 and 303,153, 9x13) pictured with the arrows its onDraw
        # draws at 313,49 and 313,153 -- screen pixels, so less the entry's x and ConvertMenu's +8.
        image={"tickerScrollUp": ("arrow_up", 2, 1), "tickerScrollDown": ("arrow_down", 2, 0)},
    ),
}

# The main menu: SWOS 2020's entries by name, in the order whose ordinals 11..16 are the
# game's own main menu's (CommonMenuExit). None = an invisible filler.
MAIN_ORDER = ["competition", "practice", "replays", "options", "editing", "extras",
              "exit", None, None, None, None,
              "continueContest", "saveContest", "loadOldCompetition",
              "multiplayer", "credits", "modes"]
# SWOS 2020 as it is built (mpgame.h defines MULTIPLAYER_MENU): main.mnu's six buttons keep
# their places, and game.cpp relabels them (SWOSMainMenuOnDraw_2, the texts padded for the
# icons as there; PRACTICE dark blue, REPLAYS purple) and gives them other jobs (the
# *MainMenu_onSelect handlers): COMPETE's slot is MULTIPLAYER, PRACTICE's COMPETE, REPLAYS'
# PRACTICE, EDITING's EXTRAS (extrasEx) and EXTRAS' REPLAYS. Per slot: text, handler, colour.
MAIN_SLOTS = {
    "competition": ("  MULTIPLAYER", "multiplayer", None),
    "practice":    ("  COMPETE", "compete", None),
    "replays":     ("  PRACTICE", "practice", 3),        # (kDarkBlue)
    "options":     ("OPTIONS   ", "options", None),
    "editing":     ("EXTRAS   ", "extras", None),
    "extras":      ("REPLAYS  ", "replays", 11),          # (kPurple)
}
# SWOS United's careers (Playa, 2026-11-19): a fourth row of buttons under PRACTICE and REPLAYS, above
# CONTINUE / LOAD -- CLASSIC CAREER (96/97's world, the season SEASON START chose) and WORLD
# 26/27 CAREER (src/career_seasons.c). The four rows 24 high, 4 apart (SWOS 2020's three were 30, 6
# apart), from y 41; CONTINUE under them. They take the ordinals of the game's own MULTIPLAYER (14)
# and MODES (16), which this menu never shows (COMPETE and PRACTICE have them): the menu replaces
# the compiled one in place, so it has no more entries. COMPETE's CLASSIC CAREER MODE goes.
ROW_Y, ROW_H, ROW_STEP = 41, 24, 28
# Each with an icon (Playa's drawings, 2026-11-26) at a column, the icon and the text centred together
# in the box -- left of the text on the left buttons, right of it on the right ones, as the rows
# above: spaces before or after the text move it (2 px each).
# .pngs, their own colours, drawn over the buttons' shading (mod_hooks.c ModHooks_FrameDrawn).
CAREER_BUTTONS = [("classicCareer", "multiplayer", 1, "     CLASSIC CAREER", 4, "classicCareer",
                   "classic_career.png", 11),
                  ("worldCareer", "modes", 155, "WORLD CAREER     ", 15, "worldCareer",
                   "world_career.png", 114)]
CONTINUE_Y = 154        # (on screen 154..173: 3 under the last row's 150, 4 over SAVE / LOAD's 178)
# The SWOS United logo's place in the CREDITS entry: SWOS 2020's 293,185 less the entry's 281,182
# and ConvertMenu's +8 (mod_menus2020.c kCreditsPicX / kCreditsPicY: keep the two the same).
CREDITS_PICTURE_AT = (293 - 281 - 8, 185 - 182)
# The logo's size (scripts/make_united_logo.py writes it): SWOS over UNITED, SWOS as wide as the
# Amiga title's "Sensible"; its ink fills it but for the last column (LOGO_INK).
LOGO_SIZE = (130, 66)
LOGO_INK = (129, 66)
# The whole menu but the logo this much lower than SWOS 2020's (2026-11-25: the stacked logo above
# it, the Amiga's look): there is room under the ticker (the picture's 272 rows, the menu's at 36).
MENU_DOWN = 10
MENU_TOP = 36               # (the menu's 0 on the picture: menu_load_json.c's +36)
# A small BETA under the logo while SWOS United is in test (False: none).
BETA = True
BETA_GAP = 2                # (rows between the logo and it)
SMALL_FONT_H = 6            # (the menus' small font's height)
# The LIVE CAST TV's size (scripts/make_menus2020_icons.py TV_SIZE): SWOS 2020's 42x38, pixel for
# pixel, at the left buttons' edge (x 1) in the band above them (y 1..38). It was four fifths as big
# from 2026-11-20 (Playa: clear of the banner); the banner is narrower instead.
TV_SIZE = (42, 38)
TV_AT = (-4, 4)             # (2026-11-25: standing on MULTIPLAYER, 5 px left of its edge)
# Inside its housing's outline (rows 9..34, columns 1..40: the antenna above, the feet below): the
# entry's box, so the frame of a chosen entry lies on the housing's edge, as SWOS 2020's does; the
# picture reaches out over it.
TV_HOUSING = (9, 26)        # (first row, rows)
TV_HOUSING_X = (1, 40)      # (first column, columns)
# (button slot, its picture, the icon's column on it): its onDraw's drawMapEx256_8sets columns
# under MULTIPLAYER_MENU, y + 7.
ICON_COLUMNS = [("competition", "multiplayer", 15), ("practice", "compete", 30),
                ("replays", "practice", 27), ("options", "options", 98),
                ("editing", "extras", 98), ("extras", "replays", 101)]
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

    # The six buttons, relabelled (MAIN_SLOTS), in rows of ROW_H.
    for name, (text, handler, colour) in MAIN_SLOTS.items():
        it = by_name[name][0]
        row = (it["coords"]["y"] - 41) // 36
        it["coords"]["y"], it["coords"]["h"] = ROW_Y + row * ROW_STEP, ROW_H
        set_text(it, text)
        for c in it["commands"]:
            if c["op"] == "onselect_fire":
                c["func"] = mod(handler)
            elif c["op"] == "colors" and colour is not None:
                c["background_color"] = colour

    # 11: the game prints "CONTINUE <name>" / "REPLAY <name>" into it (CommonMenuExit).
    set_text(by_name["continueContest"][0], deferred=True)
    by_name["continueContest"][0]["coords"]["y"] = CONTINUE_Y
    # SAVE and LOAD as main.mnu has them, with the floppies its onDraw draws
    # (disk_save_load_16x14_1x3_map at 91,182 and 211,182). drawMapEx takes screen pixels, and
    # ConvertMenu puts an entry 8 right of its .mnu x: so a picture's place in the entry is
    # SWOS 2020's x - the entry's x - 8 (and its y - the entry's y).
    for name, picture, dx in (("saveContest", "save", 91 - 77 - 8),
                              ("loadOldCompetition", "load", 211 - 155 - 8)):
        it = by_name[name][0]
        it["image"] = "grafs_2020/menus2020/%s.bmp" % picture
        it["image_x"], it["image_y"] = dx, 4
    # QUIT is a picture there: drawExitIcon(3, 181) over main.mnu's exit entry (-5,181, 31x17).
    quit_item, quit_origin = by_name["exit"]
    quit_item["commands"] = [c for c in quit_item["commands"] if c["op"] not in ("text", "text_no_ptr", "colors")]
    quit_item["commands"].insert(0, {"op": "colors", "frame_color": 0, "background_color": 0})
    quit_item["image"] = "grafs_2020/menus2020/quit.bmp"
    quit_item["image_x"], quit_item["image_y"] = 3 - (-5) - 8, 0
    # CREDITS is a picture there: the SWOS United logo, three little players standing
    # (drawSwosUnitedLogo: swos_united_logo_24x14_1x1_Map at kSwosUnitedLogoX/Y, 293,185) over
    # main.mnu's credits entry (281,182, 31x18). mod_menus2020.c's run-in brings the three players
    # there, each standing where it stands in the picture (CREDITS_PICTURE_AT is its kCreditsPicX/Y),
    # then shows the picture in their place.
    credits_item, credits_origin = by_name["credits"]
    credits_item["coords"] = {"x": 281, "y": 182, "w": 31, "h": 18}
    credits_item["commands"] = [c for c in credits_item["commands"] if c["op"] not in ("text", "text_no_ptr", "colors")]
    credits_item["commands"].insert(0, {"op": "colors", "frame_color": 0, "background_color": 0})
    credits_item["image"] = "grafs_2020/menus2020/credits.bmp"
    credits_item["image_x"], credits_item["image_y"] = CREDITS_PICTURE_AT

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
    # ... and in their places, SWOS United's careers (CAREER_BUTTONS): PRACTICE's row's look, a
    # colour each (CLASSIC brown, WORLD dark yellow), the text centred.
    row_origin = by_name["replays"][1]
    for name, slot, x, text, colour, handler, icon, icon_x in CAREER_BUTTONS:
        extra[slot] = ({"kind": "entry", "name": name,
                        "coords": {"x": x, "y": ROW_Y + 3 * ROW_STEP, "w": 148, "h": ROW_H},
                        "commands": [{"op": "text", "flags": "0x10", "literal": text},
                                     {"op": "colors", "frame_color": 0, "background_color": colour},
                                     {"op": "onselect_fire", "func": mod(handler)}],
                        "image": "grafs_2020/menus2020/%s" % icon,
                        "image_x": icon_x, "image_y": (ROW_H - 16) // 2}, row_origin)

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

    # SWOS 2020's button icons (its onDraw, game.cpp: drawMapEx256_8sets at the button's x + a
    # column, y + 7): each button's "image" (docs/MENUS.md §13) at that column. The pictures are
    # made on the player's machine by scripts/make_menus2020_icons.py; without them nothing is
    # drawn. (On the buttons themselves: this menu replaces the compiled one in place, which
    # leaves no room for entries of their own.)
    for name, picture, dx in ICON_COLUMNS:
        it, _o = by_name[name]
        it["image"] = "grafs_2020/menus2020/%s.bmp" % picture
        it["image_x"], it["image_y"] = dx, (ROW_H - 16) // 2     # (16 high: SWOS 2020's y + 7 of 30)
    # The news ticker's row (the `ticker` mod scrolls its news through it, "ticker_entry"):
    # at the bottom, under SAVE and LOAD, as SWOS 2020's (its window text at row 250 of 270:
    # 15 under the menu's 200) -- the first filler, which is never drawn. mod_menus2020.c puts
    # it back at the top on a screen with no rows under the menu.
    filler = MAIN_ORDER.index(None)
    ordered[filler][0]["name"] = "ticker"
    ordered[filler][0]["coords"] = {"x": 0, "y": 206, "w": 320, "h": 10}   # (clear of the build line)
    doc["ticker_entry"] = filler
    # LIVE CAST's TV (SWOS 2020's tvIcon: x kColumn1X + 3, y kStartY - 37, 42x38): the next
    # filler, shown -- its picture idle or live (mod_menus2020.c, ModApi_LiveAny), a click or
    # UP from COMPETE opens the live matches. Hidden by mod_menus2020.c without netplay.
    tv = ordered[filler + 1][0]
    tv["name"] = "liveCast"
    tv["coords"] = {"x": TV_AT[0] + TV_HOUSING_X[0], "y": TV_AT[1] + TV_HOUSING[0],
                    "w": TV_HOUSING_X[1], "h": TV_HOUSING[1]}
    tv["image"] = "grafs_2020/menus2020/tv_idle.bmp"
    tv["image_x"], tv["image_y"] = -TV_HOUSING_X[0], -TV_HOUSING[0]
    tv["commands"] = [{"op": "colors", "frame_color": 0, "background_color": 0},   # (a frame when chosen)
                      {"op": "nav", "left": -1, "right": -1, "up": -1, "down": MAIN_ORDER.index("competition")},
                      {"op": "onselect_fire", "func": mod("liveCast")}]

    # SWOS United's logo above the buttons: the third filler, never chosen (no handler, nothing
    # leads to it). Its picture is ours, not SWOS 2020's -- committed, grafs_united/logo.png in the
    # mod (its soft edges' alpha kept: menu_extras.c), made LOGO_SIZE by scripts/make_united_logo.py.
    # Under it, while there are test builds, a small BETA: the fourth filler, a text.
    logo = ordered[filler + 2][0]
    logo["name"] = "logo"
    beta = ordered[filler + 3][0] if BETA else None
    # centred over the buttons (1 .. 155 + 148 - 1), and between the picture's top and the first
    # row's (ROW_Y, its origin 1, MENU_DOWN lower) the same room above and below it, BETA with
    # it -- reaching up out of the menu's 0 (a y down to -MENU_TOP is still on the picture)
    first_row = MENU_TOP + ROW_Y + 1 + MENU_DOWN
    tall = LOGO_INK[1] + (BETA_GAP + SMALL_FONT_H if BETA else 0)
    logo["coords"] = {"x": round((1 + 155 + 148 - 1) / 2 - (LOGO_INK[0] - 1) / 2),
                      "y": (first_row - tall) // 2 - MENU_TOP,
                      "w": LOGO_SIZE[0], "h": LOGO_SIZE[1]}
    logo["image"] = "grafs_united/logo.png"
    logo["commands"] = [{"op": "colors", "frame_color": 0, "background_color": 0}]
    if beta:
        beta["name"] = "beta"
        beta["coords"] = {"x": logo["coords"]["x"], "y": logo["coords"]["y"] + LOGO_INK[1] + BETA_GAP,
                          "w": LOGO_INK[0], "h": SMALL_FONT_H}
        beta["commands"] = [{"op": "colors", "frame_color": 0, "background_color": 0},
                            {"op": "text", "flags": "0x7", "literal": "BETA"}]   # (the small font, grey as the build line, centred)

    for it, _o in ordered:          # (all of it lower, but the logo and BETA: placed above)
        if it is not logo and it is not beta and "coords" in it:
            it["coords"]["y"] += MENU_DOWN
    doc["items"] = emit(ordered)
    doc["name"] = "swosMainMenu"
    doc["onInit"] = mod("mainInit")
    doc["onDraw"] = mod("mainDraw")            # (each frame: the TV's live / idle picture)
    doc["onReturn"] = mod("mainDrawn")         # (... and as the menu is come back to)
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
    for name in spec.get("deferred", []):      # (a line its onDraw fills in: text_no_ptr)
        set_text(by_name[name], deferred=True)
    for name, (x, y, w, h) in spec.get("move", {}).items():
        by_name[name]["coords"] = {"x": x, "y": y, "w": w, "h": h}
    for name, (picture, dx, dy) in spec.get("image", {}).items():
        by_name[name]["image"] = "grafs_2020/menus2020/%s.bmp" % picture
        by_name[name]["image_x"], by_name[name]["image_y"] = dx, dy
    # Entries of ours, after SWOS 2020's (so its ordinals stay): a copy of `like` (its colours,
    # text flags) with our text and handler.
    for add in spec.get("add", []):
        item = json.loads(json.dumps(by_name[add["like"]]))
        item["name"] = add["name"]
        x, y, w, h = add["coords"]
        item["coords"] = {"x": x, "y": y, "w": w, "h": h}
        item["commands"] = [c for c in item["commands"] if c["op"] not in ("nav", "onselect_fire")]
        set_text(item, add.get("literal"), symbol=add.get("symbol"))
        item["commands"].append({"op": "onselect_fire", "func": mod(add["handler"])})
        if add.get("hidden"):
            hide(item)
        doc["items"].append(item)
        by_name[add["name"]] = item
    for name, (l, r, u, d) in spec.get("nav", {}).items():
        it = by_name[name]
        it["commands"] = [c for c in it["commands"] if c["op"] != "nav"]
        it["commands"].append({"op": "nav", "left": l, "right": r, "up": u, "down": d})
    doc["name"] = spec["out"]
    doc["onInit"] = mod(spec["onInit"]) if spec["onInit"] else None
    doc["onDraw"] = mod(spec["onDraw"]) if spec.get("onDraw") else None
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
