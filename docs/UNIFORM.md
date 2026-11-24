# UNIFORM — choosing the kit each side wears for a match

> **Status: current** (2026-10-02). SDL builds only: the screen is a JSON menu, which the
> Amiga build does not run, so its button is hidden there.

The PLAY MATCH screen (the team screen before every match: friendly, cup, league, career,
and the online lobby's) has a **UNIFORM** button. It opens SWOS 2020's SELECT UNIFORM screen:
both teams side by side, each with a shirt, shorts and socks, four shirt types, `-`/`+` on
every colour, SWITCH and RESET.

| control | does |
|---|---|
| the four shirt icons | shirt type: plain, coloured sleeves, vertical stripes, hoops |
| `-` / `+` | the shirt's colour, its second colour, the shorts, the socks (ten colours each) |
| FIRE + LEFT/RIGHT on the shirt, shorts or socks | the same, without leaving the drawing; FIRE + UP/DOWN on the shirt is its second colour |
| SWITCH | the team's other kit (its second if it is in its first, else its first) |
| RESET | nobody has chosen for this side: the game dresses it again |
| SELECT | back to the team screen |

## What a choice is

**A choice lasts one match and never touches the team.** SWOS 2020 wrote the kit into the
team's file, which in a career is the team the save keeps. Here it is kept beside the team
(`src/uniform_menu.c`, `Uniform_*`), forgotten when the next match is set up (`SetupPlayers`),
and applied at kick-off: `InitializeInGameTeamsAndStartGame` takes its kits from *copies* of
the two team headers, on which a chosen kit has been written over **both** of that side's kits
(`Uniform_Kits`). Nothing after that reads a kit from a team file — the match, its replay and
a rollback all work from the in-game teams.

**Offline, a chosen kit is worn as chosen** (as SWOS 2020's `g_CustomUniform`, ReportBot 0028):
when either side chose, both sides wear their first kit — a choice is both of that side's kits —
and SWOS's rule for kits that are too alike is not asked (`SetInGameTeamsFirstKits`). The
screen's help line says when the two are alike ("THESE KITS ARE ALIKE - THE GAME WILL NOT
CHANGE THEM"). A match where nobody chose is dressed by the rule (`SetInGameTeamsPrimaryColors`)
as it always was.

**The screen shows what will be worn.** A side nobody has chosen for is drawn in the kit the
rule gives it against the other side as it stands (the rule is run on the spot and what it
wrote is put back), so opening the screen and leaving changes nothing. `SWOS.log` has what the
screen showed (`[uniform] …`) and what the match kicked off in (`[match] kits: …`); they are
the same lines.

## Online

In the lobby's team screen only this player's side can be changed; the other's is shown once
they have picked a team. A chosen kit travels **inside the team**: both of its kits become the
chosen one before the team is sent (`lineup_end`), so both machines dress the side from the
same record and the recording's `SETP` carries it. Beside the team, the lobby's STATE says
whether the player chose (`kitChosen`, lobby version 10), and the host puts both players' in the
match setup (setup version 7). The team's own two kits are remembered from when it was picked
(`s_picked`, `src/online_menus.c`) so SWITCH and RESET still have them the next time the
screen is opened.

A player has **set his kit himself** once he changed it on the UNIFORM screen or left that
screen with SELECT — SELECT takes the kit shown, the game's pick included. RESET as the last
thing before SELECT hands it back to the game. A kit set this way is never changed by the game,
whatever the other player does. Both players may be on the screen at once: the other's kit is
followed from the lobby while it is up (`UniformMenu_SetOnlineHooks`), and this player's goes
out as soon as the screen is left (`NetplayLobby_SetKit`), not only with the team screen. The
lobby's little players show the kits the match will be played in.

How the match is dressed (`SetInGameTeamsOnlineKits`, `src/team_colors.c`; the host is the
first team; "chose" is "set his kit himself"):

| who chose | the kits | told before ready |
|---|---|---|
| nobody | SWOS's rule, as offline: second kit, else a one-colour kit | nobody |
| one side | that side as chosen; the other in its first kit, else its second; if both are too alike, its first | both, when the two are too alike |
| both | both as chosen | both, when the two are too alike |

"Told" is THE KITS LOOK ALIKE (both kits shown side by side, `openswos/kitsAlikeMenu`), when the player confirms the team screen or presses GO:
PLAY ANYWAY says ready, UNIFORM opens that screen and then the team screen to confirm again. A
match is played in alike kits only when both players have said PLAY ANYWAY: when the other side's
kit changes and leaves the two alike, this player is no longer ready (even where the lobby keeps
him ready through other changes, as SWOS 2020's does) and the chat says THE KITS NOW LOOK ALIKE - PLEASE CHECK THE COLOURS.
The UNIFORM screen shows the same decision. Before
NETPLAY_COMPAT 23 an online match was always played in both first kits.

## The drawings: `[graphics] kit_art = amiga | jaguar`

Default `amiga`; `--kit-art=` on the command line. It is its own setting and does not follow
`sprites`. `src/kit_art.c` draws both:

| | where the art is | how it is coloured |
|---|---|---|
| `amiga` | the MENUS bank: sprites 190-192 (shirt, shorts, socks), 199-202 (type icons) — the EDIT TEAM screen's | one shirt for all four types: its pixels are in eight colours, one per region, and a type is which regions take the second colour (`_shirtTypesTable` / `_shirtsIndexTable`, the same two tables EDIT TEAM uses) |
| `jaguar` | `bin/grafs_jag/JAGKITS.BIN`, cut from `CJCGRAFS_JAGSHEET.bmp` by `scripts/make_jag_graphics.py` (`KIT_BOXES`) | a shirt per type; each colour is four shades, in the kit entries the Jaguar sprites use (240-255) |

The Jaguar's shades need palette entries a menu does not have, so while the screen is up every
team colour's four shades (by `shade_step`) and the icons' sixteen greys are written into
entries 192-247 of the menu palette, which the menus leave unused, and given back when it
closes (`KitArt_Lend` / `KitArt_Return`, around `ShowMenu`, as the credits' photos do).

The Jaguar's drawings are taller (shirt 41 px against 29, shorts 22 against 15, socks 25
against 15), so the screen is laid out for the art in use (`lay_out`). The little player above
each shirt is the stadium screen's (sprites 210-212) in both.

**EDIT TEAM and MY CUSTOM TEAM keep the Amiga's drawings** whatever this is set to: those
screens fit two kits in 160 px, and two of the Jaguar's need 188.

## The button

Entry 68 of `playMatchMenu` — a structural edit of a compiled menu, so it is in
`bin/data_json/menus/playMatchMenu.json`, `Menu_PlayMatch` (`include/menu_data.h`) and
`src/menu_data.c` together, as the CREDITS button was. The bottom row has no room for a fifth
button, so UNIFORM shares EDIT TACTICS' place: the two are stacked, 10 px each, in the 23 the
PLAY button beside them takes; where EDIT TACTICS is not shown (computer v computer) UNIFORM
has the place alone (`UpdateUniformButton`, `src/play_match_menu.c`). It is shown whenever the
screen is about to start a match somebody can still arrange: not while the other team is being
looked at, not before a training match, not when the screen only shows a result.

## Checking it

There is no automated test of the screen. By hand, with `SWOS_KEY_SCRIPT` and
`SWOS_SCREENSHOT_INTERVAL` (docs/MENUS.md §10b) through a friendly: open the screen, leave,
and compare `[uniform]` with `[match] kits:` in `SWOS.log`; then the same after SWITCH on one
side. `bin/SWOS.x86_64 --menu-selftest` covers the menu's JSON.
