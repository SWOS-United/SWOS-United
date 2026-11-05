# SWOS2020_MODS.md — SWOS 2020's features in SWOS United, as mods

Status: **plan, decided 2026-11-04** (branch `swos2020-mods`). Nothing built yet. Source:
`C:\Projects\GitHub\swos-2020` at df38d20. The mod system is OpenSWOS's (docs/MODS.md); this
file says which SWOS 2020 feature goes where and which engine hooks each needs.

**Rule agreed with OpenSWOS (2026-11-04):** every new engine hook is built in OpenSWOS first
(MODS.md §16) and merged into United (`git merge upstream/main`); United never prototypes a
hook. United-only mod code may use only hooks that already exist. The owner decides which of
the mods below are canonical OpenSWOS mods and which are United-only.

**The owner's decisions (2026-11-04):**
- **Canonical OpenSWOS mods, built in OpenSWOS** and merged into United: `practice`, `challenges`,
  `speedrun`, `ticker` ("since these are mods we can easily move these around if we change our
  mind"). United does not start them.
- **United-only:** `menus2020`, `galore`, `united`.
- **DLC:** the challenge `.DIY`s, the historic `.EDT`s, the 2020 pitches.
- **Port SWOS 2020's own code** (`game.cpp` etc.), not petarku/game-modes.
- OpenSWOS builds H1, H2, H4 after its hair mod; H5/H6/H8/H9 go to it as specs.

---

## 1. What United already has (nothing to do)

Multiple custom teams, the uniform selector, gameplay styles (Amiga/DOS/XBLA), widescreen,
whole-match replays (`.SRP`, our own format), the PC sounds and music, the mods system itself
(SWOS 2020's DLC manager), the online menus (log-in, matchmaking, lobby, rankings, leagues).

## 2. Base, not mods (engine capabilities or tweaks: proposed to OpenSWOS)

| Feature | Why base |
|---|---|
| PC `.DIY` / `.EDT` reader (historic tournaments, challenges) | a format (MODS.md §3, PR #11) |
| Replay trick play in the viewer: pause, speed, rewind/forward, first/last frame | specified in REPLAY_FORMAT_V2 §6, not wired |
| Joypad config menu, auto-detect, disable shoulder buttons | platform |
| Mouse in menus | platform |
| File browser: sort by name, search | engine UI |
| Per-channel volumes, menu music fade in/out | audio |
| Pitch selection menu (1-5, training, random) | the setting exists; the 2020 pitches stay DLC |
| Video scalers (hqNx, xBRZ, scaleNx) | platform (SHADERS.md) |
| Kick-off back pass, GK/ref clash kits | `[tweaks]` candidates -- check against SWOS2.s first (MODS.md §4) |

## 3. Mods

| Mod (slug) | SWOS 2020 features | changes | Server? | Hooks it needs (§4) |
|---|---|---|---|---|
| `practice` | PRACTICE menu: team training (free-kick drills, CPU skill per line, ghost players), penalty shootout on its own, demo game (CPU v CPU) | gameplay (offline only) | no | H1, H2, H4 |
| `challenges` | scenario mode (`challenges.ini`: score, minute, legs, ET/pens, CPU side), stars/completion, difficulty, the custom-challenge editor, TRY AGAIN / QUICK RETRY | gameplay (offline only) | sharing and online stats only | H1, H2, H3, H4, H7, H8 |
| `speedrun` | speedrun categories (1 goal, header, 3 goals, tackle, 10 goals, yellow, red, match), team/difficulty choice, my records, retry | gameplay (offline only) | leaderboards, news | H1, H2, H3, H4, H8 |
| `galore` | SWOS Galore: share, rate, Goal of the Month / All-Time charts, view-highlight dialog | cosmetic | **yes** | H1, H6, H9 |
| `ticker` | news and tips ticker, its list, EXTRAS links | cosmetic | the feed | H5, H7 |
| `menus2020` | SWOS 2020's main menu, COMPETE / EDITING groupings, QUICK ACCESS / after-match screen | cosmetic | no | H1, H6 (a whole-menu replacement needs MENUS.md §1e's repoint) |
| `united` (MODS.md §13) | match logs and stats, my friendly/ranked stats, player info, custom-tactics warning, fun cup, LIVE CAST | cosmetic | **yes** (United's) | H9 |

**Content, always DLC (others' rights):** the challenge `.DIY` files, the historic `.EDT` sets,
the SWOS 2020 pitches -- each its own DLC mod over the canonical code mod.

**Starting point:** SWOS 2020's own code (`game.cpp`, `scenario.h`, `challenges.ini`), the
owner's choice -- not petarku/game-modes.

### menus2020: SWOS 2020's main menu as a file replacement (checked 2026-11-04)

`main.mnu` through `scripts/import_swos2020_menus.py main`, put in a test mod as
`data_json/menus/swosMainMenu.json`: **it fits** -- 542 B packed, installed in place of the
compiled main menu ("loaded from JSON (EDITED vs compiled)", 0 too-large), and it draws:
COMPETE / PRACTICE / REPLAYS, OPTIONS / EDITING / EXTRAS, CONTINUE, SAVE, LOAD. SDL only (the
Amiga's menus are compiled). But a file alone gives a menu that does nothing:

- **Every button is a no-op**: 16 of its 17 handlers are SWOS 2020's (`SWOS_competitionMainMenu_onSelect`,
  `showReplaysMenu`, `showQuitMenu`, ...), unresolved; the sub-menus they open (COMPETE =
  `competitionEx`, EDITING, EXTRAS, the REPLAYS hub) are not imported. Only OPTIONS has a
  counterpart (`OptionsMenuSelected`, under another name).
- **Its `onInit` is ours** (`SWOSMainMenuInit`, the name matches), and ours edits entries BY
  ORIGINAL ORDINAL (14 = MULTIPLAYER, 15 = CREDITS, the CONTINUE / SAVE / LOAD rows): with 14
  entries in a new order it would hide or retitle the wrong ones. The menu needs its own init.
- `OnReturn` / `OnDraw` (the TV icon, the little player, the QUIT and CREDITS icons) are not ported.
- MULTIPLAYER is not on it: SWOS 2020 reaches online play through COMPETE.

So `menus2020` is code, not data: its handlers, its init and the sub-menus -- a United source
mod that registers handlers (§8.2 item 4, not built). Each button can route to what United
already has (COMPETE -> CAREER / DIY / PRESET / MULTIPLAYER; EDITING -> custom teams, tactics;
REPLAYS -> highlights, `.SRP`), and PRACTICE to the canonical `practice` mod when it lands.

### menus2020: built, v0.1 (2026-11-04)

`src/mod_menus2020.c` (listed in `src/fork_mods.inc`), its menus made by
`scripts/make_menus2020.py` into `bin/mods/menus2020/` (canonical, off by default; OPTIONS -> MODS
or `[mods] enabled = menus2020`). SWOS 2020's main menu replaces the game's; its buttons:
COMPETE -> COMPETITIONS (ONLINE = MULTIPLAYER, DIY, PRESET, CLASSIC CAREER), PRACTICE -> FRIENDLY
MATCH + MODES (the mods' entries, when there are any), REPLAYS -> HIGHLIGHTS, OPTIONS, EDITING ->
EDIT CUSTOM TEAMS / EDIT TACTICS, EXTRAS -> the same two + INSTRUCTION MANUAL, CONTINUE / SAVE /
LOAD, QUIT (asks), CREDITS.

- **Ordinals 11..16 keep the game's meanings** (CONTINUE, SAVE, LOAD, MULTIPLAYER, CREDITS, MODES):
  CommonMenuExit sets them by ordinal, and every game handler ends in it.
- **A sub-screen never runs a game handler itself**: it records the choice and closes; the main
  button that opened it runs it with the main menu current (`open_screen`).
- **SPEEDRUNS / CHALLENGES** show when OpenSWOS's `speedrun` / `challenges` mods are on, and run
  their MODES entry (`ModApi_FindEntry` / `ModApi_RunEntry`, OpenSWOS c46c56eb).
- Not there yet: SEASON and SAVE DISK FILING (the original main menu's; SWOS 2020 has neither),
  the ticker rows, SWOS 2020's icons (TV, QUIT, CREDITS, SAVE/LOAD disks, the little
  player), menu descriptions. Checked: SDL headless -- the menu, COMPETE -> DIY -> back, PRACTICE;
  both targets build; `--menu-selftest`, the mod-match test.

## 4. Hooks to request from OpenSWOS (specs)

| | Hook | Fires | May read / write | Re-sim / netplay |
|---|---|---|---|---|
| H1 | **Mode entry points** (MODS.md §8.2 item 3): a mod registers a row (label, handler) at a named point: `main`, `friendly`, `preset-competition`, `extras` | when the menu is built | adds rows only | menus: no |
| H2 | **Match setup** (§8.4): before kick-off, a mod sets the teams, who is CPU, the starting score, minute, half/ET/pens, players sent off or injured, match length | once, before frame 0 | writes the match's starting state through an API, never raw globals | it is the match's state: such a mod is gameplay; offline only until §8.6 |
| H3 | **Match events**: goal (header/volley/penalty), tackle, foul, card, injury, half time, full time, with the frame | when the event is simulated | read-only | never on a re-simulated frame |
| H4 | **Match end / next screen**: the result, and the mod picks where play goes next (retry, its own menu) instead of the default | at the final whistle | the result read-only; returns a menu | after the confirmed end only |
| H5 | **Menu drawn**: draw over every menu frame (a ticker line) | each shown menu frame | draws only | menus: no |
| H6 | **After match**: a row/screen after a match (share highlight, save replay) | after the match ends | reads the match's replay/highlights | no |
| H7 | **Mod settings** (§8.2 item 2): `Mods_Setting(uuid, key)`, `[mod.<slug>]`, the mod's page | — | — | a gameplay setting travels with the mod (§8.6) |
| H8 | **Mod save data**: a file per mod in the save folder (records, stars), `Mods_SavePath(slug, name)`, never an overlay dump | when the mod saves | its own file only | no |
| H9 | **Online provider** (§13): which server, log-in, where rankings, leagues, uploads come from | — | — | the United mod's |
| H10 | **Handler registration** (§8.2 item 4): a built-in mod registers `<slug>.<name>` functions (onInit/onReturn/onDraw/onSelect); the menu loader resolves them only while `Mods_IsOn(slug)`, else no-op | when a menu loads | its own menus | menus: no; SDL only |

## 5. Order (proposed)

1. `practice` -- needs H1, H2, H4; no server; the smallest mode that proves the match-setup hook.
2. `challenges` (+ the challenge DLC, once the `.DIY` reader is base) -- adds H3, H7, H8.
3. `speedrun` -- reuses H1-H4, H8.
4. `ticker`, `menus2020` -- cosmetic, H5/H6.
5. `galore`, `united` -- after H9, with United's server.

## 6. The imported menus that do nothing yet (2026-11-04)

| Menu | Its data in SWOS 2020 | Status |
|---|---|---|
| NEWS | speedrun "LATEST RUNS" (`viewNewsCurrent_v2`, game.cpp:10233) | comes with the canonical `speedrun` mod (agreed) |
| MATCH LOGS (`mLogReader`/`mLogTicker`/`mLogStats`), MY FRIENDLY / RANKED STATS | SWOS 2020's galore API, `sensiblesoccer.de/swos2020/galoreapi_v15.php` (`list_log_files`, `get_my_*_stats`) -- SWOS 2020's database, not United's matches | **owner's decision** whether United uses it (`united_leagues.c` hides MY STATS until United's server keeps matches) |
| MATCH INFO, PLAYER INFO | `loadPlayerAndMatchInfo` (mpgame.cpp:10648), from a matchmaking user row; `userdetail` on sensiblesoccer.de, which United already calls | waits for `Account_UserSelected(name)` (online_account.h), in OpenSWOS's hook batch; then a United port in United's files |
| FUN CUP | local `MP\DB\*.EDT` custom databases (game.cpp:22770) | **owner's decision**: custom databases vs the online rule (database teams only) |
