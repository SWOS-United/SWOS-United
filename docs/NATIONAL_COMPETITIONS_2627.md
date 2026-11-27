# World 2026-27's national competitions (branch NATIONAL-TEAM-COMPS)

National-team careers in **World 2026-27 only** get today's competitions: a 48-team World Cup, the
real 2026 qualifying in every confederation, a 24-team Euro, the UEFA Nations League and the
continental cups with qualifying rounds. Classic careers (96/97) and Europe 1984-85 keep their own
national competitions unchanged: every hook checks `World_Current() == WORLD_2627`.

**The spec** (every format, the calendar, the holders, the decisions): the owner's doc
"World 2026-27 National Competitions — Spec", https://claude.ai/artifact/HGRx6Tno5ZqgATorn4Cxsw.
Target: the release of 2026-12-08.

## Decisions (the owner, 2026-11-26)

- Real calendar by the season's year: 2026/27 Nations League (+ AFCON 2027, Gold Cup 2027);
  2027/28 Euro qualifying + Euro 2028 (+ Asian Cup Dec 2027-Jan 2028, Copa América 2028, OFC
  Nations Cup); 2028/29 Nations League + non-UEFA World Cup qualifying, first half; 2029/30 UEFA
  qualifying + the rest + the inter-confederation play-off (March) + World Cup 2030; then every
  4 seasons. In Europe qualifying and finals share one season. A coach joins wherever the season is.
- No host qualifies automatically anywhere (the game does not model where a tournament is held);
  the holder does not either. Holders are a record only.
- UEFA World Cup qualifying: 12 groups, winners direct; 12 runners-up + the 4 best thirds in
  4 play-off paths. No Nations League play-off places.
- Euro: 10 groups, top 2 (20) + a play-off of the 8 best thirds (4 single ties).
- Nations League: UEFA only, every European team, in the seasons after a tournament; leagues A-D
  by ranking, direct promotion/relegation; replaces the Invitation tournament.
- Continental cups with qualifying rounds, so a low-ranked team can still qualify.
- Russia stays banned (UEFA 54). Confederations from the owner's CSV (Australia AFC, Papua New
  Guinea OFC, Mauritius CAF, whatever their team file).
- FIFA ranking (2026-11-25) moving with every result, saved with the career.
- Old World 2026-27 national saves switch to the new system at their next season.
- Amiga: wanted too, but its World 2026-27 still plays 96/97's 143 national teams (only SDL loads
  the 211) -- open question for the owner.

Holders at 2026-11-26: World Cup Spain (2026), Euro Spain (2024), Nations League Portugal
(2024-25), Copa América Argentina (2024), AFCON Senegal (2025, restored by CAS 2026-11-24), Asian
Cup Qatar (2023), Gold Cup Mexico (2025), OFC Nations Cup New Zealand (2024).

## Phases

1. **Ranking** -- DONE on this branch (below).
2. Real-year calendar, the 24-team Euro, the five continental cups with their qualifying.
3. The 48-team World Cup (7 stages: custom, like the 36-team league phase).
4. Qualifying, UEFA and CONMEBOL (simulated for everyone, only the coach's stage played -- the
   pattern of `src/uefa_access.c` / `src/uefa_coef.c`); Euro qualifying.
5. Qualifying, CAF / AFC / CONCACAF / OFC and the inter-confederation play-off.
6. Nations League.

## Phase 1: the ranking

- `scripts/worldgen/nat_ranking.py --csv <teams.csv> --ranking <FIFA page.html> [--world DIR]`
  writes `competitions/national_ranking.json` in the world folder (gitignored world data: copy it
  to another machine's world folder, or rerun the script there). The owner's two inputs:
  `mens_soccer_national_teams.csv` and FIFA's men's ranking page saved from the browser.
  A copy of the generated file is in git: `scripts/worldgen/data/national_ranking_2627.json`.
  On another machine, copy it to the world folder's `competitions/national_ranking.json`
  (Windows: `Documents\SWOSUnited\mods\world2627\data_json\worlds\2627\competitions\`, or
  `bin/data_json/worlds/2627/competitions/` for the golden test).
- `src/nat_rank.c` / `include/nat_rank.h`: the base from that file (`DataJson_LoadNationalRanking`,
  `src/save_format_json.c`, called beside `DataJson_LoadNationalTeams` in `frontend.c` and
  `world_switch.c`); without it, the records' skill. A career copies it at first use, moves it after
  every match between two national teams (hooks in `src/diy_competition.c`, league and cup paths)
  by FIFA's formula, weights 10 friendly / 25 qualifier / 35-40 continental finals / 50-60 World
  Cup, shoot-outs 0.75/0.5, no loss for a knockout defeat in a final tournament. W_e from a table
  (no libm on the Amiga). Logged as `[nat-rank]` in SWOS.log.
- Saved as `"national_ranking"` (team words + points in hundredths); reset for a new career.
- Friendlies and invitation tournaments pick opponents within 150 ranking points
  (`NatRank_PickOpponent`, `src/career_season.c`), widening until there are enough; banned never.
- Golden career `ENG2627-NATIONAL` added (needs the world's data in `bin/data_json/worlds/2627`;
  record it with `--update` on the Linux machine). First run (macOS, 3 seasons): 310 national
  matches moved the ranking, no panic; Euro knockout losers +0.00 as meant.

Not yet checked: the existing golden careers unchanged (Linux), the Amiga build, a save reloaded
mid-career continuing the ranking exactly.
