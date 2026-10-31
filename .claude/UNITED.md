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

- `third_party/swos-api` -- private submodule, github.com/SWOS-United/SWOS-API: the website's
  API (SWOS 2020's php_manager) and the game's C++ client for it. Its secrets are never in git
  (its README): `swos_api_config.php` on the server, build defines for the client.
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
