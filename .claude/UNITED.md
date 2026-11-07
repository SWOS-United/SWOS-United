# SWOS United — what this fork adds to OpenSWOS

SWOS United is its own project, built on OpenSWOS (github.com/benbaker76/OpenSWOS) with SWOS
2020's online play brought in: its own servers, an account and a log-in for multiplayer. Its
online play is its own -- it does not meet OpenSWOS players.

**Taking OpenSWOS's changes is this team's choice**, not an obligation (`docs/FORKING.md`):
`git fetch upstream && git merge upstream/main` takes everything since the last merge (never
rebase), `git cherry-pick <commit>` one fix, or nothing at all. `upstream` is fetch-only here;
`rerere` is on. While merges are wanted, they stay cheap if:

- this fork's differences stay in files OpenSWOS does not edit where it can:
  `include/project_identity.h` (name, online identifiers, servers, player folder), this file,
  `CONTRIBUTING.md` (on a merge conflict keep ours), and what only SWOS United has;
- `.claude/CLAUDE.md` stays upstream's; notes for this fork go here.

## What only SWOS United has

- **Accounts** (`PROJECT_ACCOUNTS 1`): `src/online_account_united.c` -- the log-in screen asks
  for a sensiblesoccer.de USERNAME / PASSWORD and checks them at LOGIN (a POST to the site's
  `com_swosapi` `view=user`, `section=login`: "<user id>;<stream key>", "0;" refused);
  REGISTER / MY PASSWORD? / MY USERNAME? open the site's pages.
- **HTTPS** (`src/https_client.c`, `include/https_client.h`): mbedTLS 3.6.2 vendored in
  `third_party/mbedtls` (Apache-2.0 OR GPL-2.0-or-later), built by `fork.mk` (native netplay
  builds; `SWOS_HTTPS`), certificates checked against `bin/cacert.pem` (Mozilla's, from
  curl.se -- refresh it now and then) and the host name. SWOS 2020 ran curl `--insecure`.
  `SWOS_HTTPS_TEST=<url> [SWOS_HTTPS_TEST_FORM=a=1&b=2]` fetches one and exits (run from bin/).
- `src/united_api.c` / `include/united_api.h`: the API's addresses, who is logged in.
- **RANKINGS** (`src/united_rankings.c`): SWOS 2020's rankings / ranking screens, AMIGA
  (`view=ranking&id=5`) and PC DOS (13); no XBOX.
- **LEAGUES** (`src/united_leagues.c`): SWOS 2020's ONLINE LEAGUES screens -- the 13
  associations, a tournament's tables (a round, a group: NEXT ROUND / NEXT GROUP, < > its other
  tournaments, APPLY its page), a group's MATCHES and a cup round's ties. The season's
  tournaments come from `view=season`, which has nothing until the site's season is set up
  (Playa, 2026-11-01: an SQL error until then, "SEASON DATA NOT AVAILABLE FOR NOW" in the
  game); a tournament from `view=tournament&id=` (past seasons' work: 6300-6319 are 10/23's).
  `SWOS_UNITED_SEASON_FILE=<xml>` stands in for the season, `SWOS_UNITED_TEST_LEAGUE=3:1:0:m`
  (with `--menu=swos2020/leagueInfoViewMenu`) opens a league, round, group and MATCHES. MY
  FRIENDLY / RANKED STATS are hidden: SWOS 2020's own server's, to come with MATCH LOGS.
- **Ranked and league / cup matches** (`src/united_matches.c`, OpenSWOS's lobby hooks in
  `include/online_account.h`). OPEN asks FRIENDLY / RANKED MATCH / LEAGUE / CUP (SWOS 2020's
  openLobbyMenu); LEAGUE / CUP with no open match turns the screen into "not signed", whose
  buttons open ONLINE LEAGUES and the Discord. In the lobby both players' SRN for the game's
  style are bot lines (`view=userdetail`, ranking 5 or 13); a ranked match the ratings do not
  allow (`login_check.php`'s `[SRN_DIFF_LIMIT]`, 750 / 1200, plus SWOS 2020's Amiga rule at
  2100) is turned into a friendly by the host, as is one whose ratings could not be fetched.
  START waits for that check, and for SELECT MATCH in league / cup (the first open leg between
  the two players in a tournament of the game's style, `view=matches`). Results: **each player
  sends their own** with their own account (`view=matchleginsert`, POSTed: lid, pg/og the
  ninety minutes, pe/oe extra time, pp/op penalties). A ranked match is made on the site by the
  host after it is played (`view=matchcreate`, createOOO, then createOOOleg for a rematch,
  keyed by `include/united_secrets.h`), and its ids reach the joiner in the lobby's rules -- so
  a joiner who leaves before they arrive has not sent their score. SWOS 2020 sent the joiner's
  password to the host instead: never do that.
- **swos.swosunited.com**, the site taking over from sensiblesoccer.de (JSON API, SWOS-API
  issues 1-4; `PROJECT_ACCOUNT_API`): `[netplay] account_site = swosunited` in SWOS.ini (or
  `SWOS_UNITED_SITE=swosunited`) moves the log-in (`api/auth/key`, its `api_key` kept for the
  session), RANKINGS (`api/rankings/users`, active players), LEAGUES (`api/seasons/current`,
  `api/tournament`) and RANKED matches there; `sensiblesoccer`, the default, keeps the old site.
  A ranked match there: the SRNs from both rankings, `swos-matches/create` by the host after the
  first match and `add-leg` after each later one, each player's own `report` (scores from their
  side: `waiting`, then `applied`), and `close` on leaving the lobby by the player whose report
  applied the last leg -- Elo counts a session only when it is closed, and closing deletes legs
  not yet applied. LEAGUE / CUP there (issue 5): the open legs from `api/open-matches`
  (`league_matches`, each with its `duel_ranking_id` for SELECT MATCH's style check), reported
  with the same `swos-matches/report`; no create or close (the tournament system's). The relay
  test below with `SWOS_UNITED_SITE=swosunited` in both `HOST_ENV` and `JOIN_ENV` plays either
  (dry run: create, add-leg, both reports, the close; or SELECT MATCH and both reports -- TESTER1
  and TESTER2 have open legs of "testtourna1" there). A joiner that leaves before the host's
  next leg id arrives closes the session without that leg (it never reported it). `src/united_api.c` reads its JSON (decodes `\/` and
  `\u00XX`, which mjson gives up on).
- **SWOS 2020's challenges** (`bin/mods/swos2020-challenges`, canonical, on by default): its 92
  scenarios in 8 categories, written by OpenSWOS's `scripts/convert_swos2020_challenges.py` from
  SWOS 2020's own `challenges/` (github.com/starwindz/swos-2020: `challenges.ini` and its
  `.EDT` / `.DIY` teams, which become `teams/*.EDT`) -- regenerate it with the script, never edit
  it by hand. OpenSWOS's challenges mod plays it (format v2, docs/MODS.md §18); its LOCKED
  categories stay closed, as in SWOS 2020. Online, the online-records mod READS SWOS 2020's records
  (by its scenario name and category); nothing is ever sent to sensiblesoccer.de -- "Nothing from
  OpenSWOS should go to sensiblesoccer.de" (the owner, 2026-11-07): the wins and speedrun runs it
  used to send are off (`UNITED_WRITE_RECORDS` 0 in src/mod_online_records.c) until SWOS United's
  own server takes them, and SHARE / SWOS Galore's uploads are never pointed there.
- **SWOS 2020's challenge screens** (`src/united_challenges.c`, mod `challenges2020`, on by
  default): MODES / COMPETE -> CHALLENGES opens SWOS 2020's SELECT CATEGORY (six a page, MORE,
  CUSTOM CHALLENGES), a category's challenges twelve a page with stars, tick / lock and the
  five description lines, a challenge's page (PLAY SCENARIO, VIEW ONLINE STATS) and SCENARIO
  COMPLETION -- its menus imported (`import_swos2020_menus.py`, the "challenge mode" group),
  the challenges read and played through OpenSWOS's `challenges` service. Online lines come from
  online-records (`challenges.wins / winners / ranking`: one download of SWOS 2020's
  completions, a row a win). `SWOS_CHALLENGES2020_TEST=<category>[/<id>]` with
  `--menu=swos2020/<screen>` opens one straight (the check: there is no scripting of menu keys).
  Not drawn yet: the uniforms, red cards and injuries on a challenge's page.
- **SWOS 2020's custom challenges** (in `src/united_challenges.c`; SELECT CATEGORY's CUSTOM
  CHALLENGES): CREATE (SWOS 2020's NEW / LOAD and its design screen -- teams, nickname, score,
  minute, second leg, ET / pens / a shoot-out alone, subs, attack direction, pitch, the starting
  team and event, win condition, difficulty; TEST plays it once, SAVE keeps it as one of the
  player's own through the challenges service's save, DELETE; no uniforms, cards / injuries or
  pitch set yet; `SWOS_EDIT_TEST=show|save` checks it), SHARE (not yet: SWOS
  2020 uploads to its server with no log-in -- the owner's call), PLAY -- the 274-odd challenges
  SWOS 2020's players shared on sensiblesoccer.de, its SelectSCN list (home, score, away, creator,
  level stars; COMPLETION; five sortings). One picked is downloaded (online-records
  `custom.download`), unpacked (`src/united_scn.c`: a .SCN is a 7z of challenge.ini and two .TEAM;
  the LZMA SDK's decoder, `third_party/lzma`, public domain) and made one of the player's fetched
  challenges -- its teams an `.EDT` and the challenge in `shared.json`, the challenges mod's save
  folder (OpenSWOS cea125ac) -- then opened on its page. Known online as SWOS 2020 knew it:
  category CUSTOM, its full name (`record_name`). `SWOS_SCN_IMPORT_TEST=<file.SCN>` or
  `=online:<server file name>` (with `--menu=swos2020/selectCategoryMenu`, `SWOS_HARNESS_TURBO=1`)
  imports one and prints its fields.
- **SWOS GALORE** (`src/united_galore.c`, mod `galore2020`, on by default; MODES -> SWOS GALORE):
  SWOS 2020's menu and its chart -- ALL TIME and MONTHLY (a year back), seven a page with the
  average rating in stars; a pick downloaded (online-records `galore.download`: the server's
  folders are case-sensitive, the name as it spells it) and shown in the viewer, its scene alone
  (OpenSWOS's `ModApi_ViewHighlights`, copied to the save folder as GALORE.HIL). RATE and SHARE
  HIGHLIGHTS say they come with SWOS United's own server (they would write to sensiblesoccer.de).
  `SWOS_GALORE_TEST=<rank>` (with `--menu=swos2020/swosGaloreMenu`) fetches, downloads and plays one.
- **SWOS 2020's speedrun screens** (`src/united_speedrun.c`, mod `speedrun2020`, on by default):
  MODES / COMPETE -> SPEEDRUNS opens SWOS 2020's SPEEDRUN (eight targets), a target's difficulty
  (SELECT TEAMS or the five preconfigured pairs -- OpenSWOS's, from 96/97's database, one per
  SWOS 2020 band: its were the PC game's TEAM.072 clubs; YELLOW / RED CARD teams only), the
  LEADERBOARD (style, target, difficulty, ten places a page), VIEW MY RECORDS (logged in: the
  player's rows at the style; UPDATE fetches again; RESET hidden, SWOS 2020 deleted the rows) and
  LATEST RUNS (the log-in group's newsMenu, now drawn). Runs through OpenSWOS's `speedrun`
  service; the boards through online-records (`speedrun.board / mine / news`: SWOS 2020's
  swosapi_v3, read-only). TACKLE is SWOS 2020's rule since OpenSWOS 35e741dd (a goal with your
  tackle the last thing before it), and a preset run sends its difficulty.
- **Secrets**: `include/united_secrets.h` (gitignored; `include/united_secrets.sample.h` is the
  template, the file itself is in SWOS-API-secrets.zip). Without it a build cannot host a
  ranked match.
- **Tests**: `SWOS_UNITED_LOGIN=TESTER1 SWOS_UNITED_ACCOUNTS=$PWD/.test-accounts` logs in at
  start; `SWOS_UNITED_DRY_RUN=1` sends no result and creates no match (logs what would go,
  without the password or key); `SWOS_UNITED_TEST_SELECT=1` presses SELECT MATCH. Through
  OpenSWOS's relay test, with game data from an OpenSWOS checkout:

      A="SWOS_UNITED_ACCOUNTS=$PWD/.test-accounts SWOS_UNITED_DRY_RUN=1"
      SWOS_ASSETS=../OpenSWOS/bin HOST_NAME=TESTER1 JOIN_NAME=TESTER2 RELAY=0 RECORD=1 \
        HOST_ENV="$A SWOS_UNITED_LOGIN=TESTER1 SWOS_LOBBY_MODE=ranked" \
        JOIN_ENV="$A SWOS_UNITED_LOGIN=TESTER2" scripts/relay_test.sh

  (`SWOS_LOBBY_MODE=league SWOS_UNITED_TEST_SELECT=1` for league / cup.) A run without the dry
  run changes the testers' real records: ranked matches move their SRN.
- **Test accounts** (TESTER1, TESTER2 on sensiblesoccer.de; the site's ticker leaves them out):
  their passwords are in `.test-accounts` at the checkout's root, `user:password` a line,
  gitignored -- never in git, a log or a commit message.

- `third_party/swos-api` -- submodule, github.com/SWOS-United/SWOS-API (private for now): **the
  home of SWOS United's online API, both sides** -- the website's `/api/*` endpoints and the
  game's client for them (the lead developer, 2026-11-07). New endpoints and client code go
  THERE, never in this repository's `src/`, which only calls the client. The API is documented
  in its issues (log-in `POST /api/auth/key`, `api_key` with every call; users, rankings,
  seasons, tournaments, matches). Started afresh 2026-11-07 (46ae08c): SWOS 2020's old API, its
  `php_manager`, is SWOS 2020's and stays in its own repository. Secrets never in git: ignored
  files with a `.sample` beside each (its README).
- `.gitmodules` lists it beside OpenSWOS's submodules: a merge that brings a new upstream
  submodule keeps both entries.

## Its identity (2026-10-31)

`include/project_identity.h`: SWOS United, tag `SWOS_UNITED` / GGPO session `swos_united` (not
`SWOSUNITED`, which OpenSWOS sent before its 2026-10-18 rename -- old tester builds still do),
player folder `SWOSUnited` / `~/.swosunited`, matchmaking server **online.swos.eu** (Redhair's
new machine, 2026-10-31; its checkout pulls this repo with a READ-ONLY deploy key,
"online.swos.eu deploy: SWOS-United (read-only)", through an ssh `Host github-swos-united`
alias). us.swos.eu, his older machine, ran OpenSWOS's matchmaker until online.openswos.net
replaced it. OpenSWOS's admin login (SWOS_ADMIN_*) is online.openswos.net's ONLY -- never send
it to any swos.eu host.
Setting the server up from scratch: `docs/SERVER_SETUP.md` (this fork's own; its nginx site is
`packaging/server/nginx/online.swos.eu.conf`).
