# SWOS United — what this fork adds to OpenSWOS

SWOS United is OpenSWOS (github.com/benbaker76/OpenSWOS) with SWOS 2020's online play brought
in: an account and a log-in for multiplayer. Everything else -- the engine, netplay and
GGPO-X, World 2026-27, the data setup, the tests -- comes from OpenSWOS by merge.

**Read `docs/FORKING.md` first.** In short:

- Work both projects need is done in OpenSWOS and merged here:
  `git fetch upstream && git merge upstream/main` (never rebase). `upstream` is fetch-only
  here; `rerere` is on.
- This fork's differences stay in files OpenSWOS does not edit: `include/project_identity.h`
  (name, online identifiers, servers, player folder), this file, `CONTRIBUTING.md` (its own
  issues; on a merge conflict keep ours), and what only SWOS United has.
- `.claude/CLAUDE.md` stays upstream's; notes for this fork go here.

## What only SWOS United has

- `third_party/swos-api` -- private submodule, github.com/SWOS-United/SWOS-API: the website's
  API (SWOS 2020's php_manager) and the game's C++ client for it. Its secrets are never in git
  (its README): `swos_api_config.php` on the server, build defines for the client.
- `.gitmodules` lists it beside OpenSWOS's submodules: a merge that brings a new upstream
  submodule keeps both entries.

## Not decided yet

- Whether SWOS United keeps OpenSWOS's online identifiers (plays OpenSWOS players) or has its
  own (`PROJECT_NETPLAY_TAG`, `PROJECT_MM_HOST` in `include/project_identity.h`).
