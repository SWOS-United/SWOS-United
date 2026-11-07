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
