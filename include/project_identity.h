/*
 * project_identity.h -- what this project is called, and how it is told apart online.
 *
 * A fork of OpenSWOS (SWOS United, github.com/SWOS-United, is one) changes THIS file, and only
 * this one, for its name, its online identifiers and its servers, so that a fork which merges
 * OpenSWOS's later changes never conflicts over them (docs/FORKING.md). Every string the
 * player sees that names the project, and every identifier another build or a server
 * compares, comes from here. Shared by the game (both targets), OpenSWOS Setup
 * (tools/swosdata) and the matchmaking server (third_party/matchmaker).
 *
 * Two builds meet online only when PROJECT_NETPLAY_TAG, PROJECT_GGPO_GAME and NETPLAY_COMPAT
 * (netplay_compat.h) agree: a fork that keeps OpenSWOS's tags plays OpenSWOS players, one
 * with its own does not.
 */
#ifndef PROJECT_IDENTITY_H
#define PROJECT_IDENTITY_H

/* The name, as the player reads it (window title, build line, messages). */
#define PROJECT_NAME            "SWOS United"
#define PROJECT_NAME_UPPER      "SWOS UNITED"         /* (the game's menus: capitals) */
/* Where it lives (the setup's About window). */
#define PROJECT_URL             "github.com/SWOS-United/SWOS-United"
/* Its data setup (tools/swosdata): the window, and what messages tell the player to run. */
#define PROJECT_SETUP_NAME      PROJECT_NAME " Setup"
#define PROJECT_SETUP_PROGRAM   "swosunited-setup"    /* (its executable, its temp folder) */

/* Online. The matchmaking server lists a game only to players with the same tag
 * (VERSION:<tag>/<NETPLAY_COMPAT>, mm_client.c; the server's Connections.cpp), the web
 * build's with -WEB after it; GGPO-X's session name; a custom team file's format. */
/* (not "SWOSUNITED": OpenSWOS's own tag before its rename, 2026-10-18 -- old tester builds
 * still send it) */
#define PROJECT_NETPLAY_TAG     "SWOS_UNITED"
#define PROJECT_GGPO_GAME       "swos_united"
#define PROJECT_TEAM_FORMAT     "swosunited-team"
/* The public matchmaking server (and recorder, reports, team check): its host. */
#define PROJECT_MM_HOST         "online.swos.eu"

/* The player's folder: saves, SWOS.ini, SWOS.log, the assets the setup makes (swos_io.c,
 * tools/swosdata SwosData_DefaultAssetsDir) -- Documents/<it> on Windows, the app-data
 * folder <it> on macOS and the Switch, ~/<dot dir> elsewhere. */
#define PROJECT_USER_DIR        "SWOSUnited"
#define PROJECT_USER_DOT_DIR    ".swosunited"

/* Accounts (include/online_account.h): a SWOS United player logs in to sensiblesoccer.de --
 * src/online_account_united.c; OpenSWOS's src/online_account.c compiles to nothing. */
#define PROJECT_ACCOUNTS        1

/* The phone apps (packaging/android, packaging/mobile/CMakeLists.txt; docs/MOBILE.md): the Android
 * application id and the iOS bundle id -- an app's identity in its store and on the phone, so a
 * fork's own apps install beside OpenSWOS's. Read from here by the mobile build. */
#define PROJECT_APP_ID          "com.swosunited.game"   /* (SWOS United's own, from swosunited.com: beside OpenSWOS's on a phone) */

/* The phone apps' link scheme: OpenSWOS Setup's Wi-Fi copy opens the app with
 * <scheme>://get?h=<address>&k=<key> (include/phone_copy.h). A fork's own, so a link opens its app.
 * Lower case, letters only. */
#define PROJECT_URL_SCHEME      "openswos"

/* The project's public site (packaging/web/site): the Wi-Fi copy's QR code is
 * https://<site>/get/#h=<address>&k=<key> -- a page any phone's camera opens without reaching the
 * local network (Android 17 lets a browser reach it only with a permission), whose button opens
 * the app; on Android it opens the app directly (an App Link: <site>/.well-known/assetlinks.json). */
#define PROJECT_SITE            "openswos.net"

/* SWOS United's own site, taking over from sensiblesoccer.de: its JSON API (the SWOS-API repo's
 * issues 1-3). [mod.menus2020] account_site = swosunited (or SWOS_UNITED_SITE=swosunited) uses it for
 * the log-in, the rankings and the leagues; sensiblesoccer, the default, keeps the old site.
 * Ranked and league results still go to sensiblesoccer.de (the account ids are the same). */
#define PROJECT_ACCOUNT_API     "https://swos.swosunited.com/api/"

/* The challenges' and speedruns' records online (src/mod_online_records.c): where SWOS 2020's
 * galoreapi_v15.php and swosapi_v3.php are -- SWOS-API (third_party/swos-api) once it is
 * deployed; today SWOS 2020's own folder on sensiblesoccer.de. No trailing slash. */
#define PROJECT_RECORDS_API     "https://sensiblesoccer.de/swos2020"

/* The Discord posts (src/live_ticker.c): SWOS 2020's relay on sensiblesoccer.de, which keeps the
 * webhooks and posts to its server's LIVE TICKERS and LIVE SCORE BOARDS channels; the game sends a
 * channel name ("live_ticker_friendly", ...) and the text, never a webhook. "" posts nothing. */
#define PROJECT_DISCORD_PROXY   "https://sensiblesoccer.de/swos2020/discord_proxy.php"

/* Where SWOS 2020 keeps its players' custom teams: <USER>.TEAM, <USER>_2.TEAM, <USER>_3.TEAM
 * (MY CUSTOM TEAMS' IMPORT FROM SWOS 2020, src/united_teamimport.c). */
#define PROJECT_SWOS2020_TEAMS  "https://sensiblesoccer.de/swos2020/mp/db/teams/"

/* SWOS 2020's ban list, "NAME, MODE, *" a line (src/online_account_united.c): read at each log-in. */
#define PROJECT_BAN_LIST        "https://sensiblesoccer.de/swos2020/ban-users.txt"

/* Where the project hosts World 2026-27's package and its credits (scripts/make_world_package.sh
 * writes both; OpenSWOS Setup's World row downloads the zip and links the credits): OpenSWOS on its
 * own site since 2026-11-12 (scripts/upload_dlc.sh -> openswos.net/dlc/), a fork on its own DLC
 * host. "" for both hides the World row (a fork that does not offer it). SWOS United: its DLC host
 * (the DLC-Manager's dlc/swosutd/world/, 2026-11-12 and -14). */
#define PROJECT_WORLD_PACKAGE_URL "https://www.sensiblesoccer.de/swos2020/dlc/swosutd/world/OpenSWOS-World-2627.zip"
#define PROJECT_WORLD_CREDITS_URL "https://www.sensiblesoccer.de/swos2020/dlc/swosutd/world/OpenSWOS-World-2627.txt"
/* The same for Europe 1984-85 (DaniSwos's database: docs/WORLD_2026_27.md §6g). "" hides its link
 * (the Worlds row stays while either world has one). */
#define PROJECT_WORLD8485_PACKAGE_URL "https://www.sensiblesoccer.de/swos2020/dlc/swosutd/world/OpenSWOS-World-8485.zip"
#define PROJECT_WORLD8485_CREDITS_URL "https://www.sensiblesoccer.de/swos2020/dlc/swosutd/world/OpenSWOS-World-8485.txt"

/* SWOS 2020's online-menu entries for features this project will not have -- the log-in's NEWS,
 * MULTIPLAYER SETTINGS' commentary and chat rows, JOIN AMIGA SUPER LEAGUE, JOIN DISCORD, HELP
 * DESK (src/online_menus.c): 1 hides them, 0 keeps them for a fork that builds them. */
#define PROJECT_HIDE_SWOS2020_EXTRAS 0       /* (SWOS United builds them: kept, the owner 2026-11-02) */

/* MATCH MAKING as SWOS 2020 draws it (src/online_menus.c, its matchMakingMenu_OnDraw_New): the
 * seven-row table always drawn, MODE and JOIN / REQUESTED / ACCEPT, the red page arrows, HOST
 * MATCH / CANCEL, the host accepting joiners from the table. 0: OpenSWOS's own screen. */
#define PROJECT_SWOS2020_MATCHMAKING 1

/* [tweaks] (include/tweaks.h, docs/MODS.md §4): 1 builds them -- SWOS.ini's [tweaks] section and
 * the mods' ModApi_SetTweak; 0 leaves them out, the original's behaviour only and no [tweaks] in
 * SWOS.ini. `make SWOS_TWEAKS=0|1` overrides it for one build. Either way they are off online, so
 * a build without them still plays one with them. */
#define PROJECT_TWEAKS          0   /* SWOS United: no [tweaks] (2026-11-05) */

/* [tweaks] stronger_goalkeeper_ai alone (SWOS 2020's --stronger-goalkeeper-ai; GAMEPLAY OPTIONS'
 * STRONGER KEEPERS): 1 builds that one tweak even when PROJECT_TWEAKS is 0 -- a fork that leaves the
 * other tweaks out can still offer the launcher's keeper option. Off line only, like every tweak. */
#define PROJECT_TWEAK_STRONGER_GK 1   /* SWOS United: this one tweak, as SWOS 2020's launcher offered it (the owner, 2026-11-10) */

/* The tweaks that are ON by default (include/tweaks.h TWEAKS_DEFAULT_ON): the fix of the
 * originals' ball-contest lookup and SWOS 2020's career league rules (its "career crash fixes",
 * 3 points for a win, 5 substitutes). 1 builds them even when PROJECT_TWEAKS is 0, as the
 * stronger keepers above; RULES OPTIONS shows them. Off line only, like every tweak. */
#define PROJECT_TWEAK_FIXES     1   /* SWOS United: SWOS 2020's league rules and the contest fix, on (the owner, 2026-11-10) */

/* STATIC MODS (docs/MODS.md §14): canonical mods -- folders in the game's own mods/ -- that are
 * part of this game rather than a choice: always on, whatever [mods] disabled says, and never
 * listed, neither on OPTIONS -> MODS (nor in its "N ON") nor on the setup's Mods screen. Slugs,
 * separated by commas or spaces. OpenSWOS: none -- every one of its mods can be turned off. A
 * fork lists the mods its game is built on. A downloaded (DLC) mod is never static. */
/* SWOS United: every SWOS 2020 mod is part of the game (the owner and Playaveli, 2026-11-07) --
 * its main menu, its challenge and speedrun screens, SWOS Galore, the news ticker, and the
 * records read from SWOS 2020's servers. */
#define PROJECT_STATIC_MODS     "menus2020, challenges2020, swos2020-challenges, speedrun2020, galore2020, ticker, online-records, discord2020, teamimport2020"

/* DEFAULT MODS: canonical mods this game turns ON by default whatever their manifest's "default"
 * says -- still listed, and a player may turn them off ([mods] disabled), unlike a static one.
 * Slugs, separated by commas or spaces. OpenSWOS: none (each mod.json's "default" decides). A
 * fork lists the ones its look is built on (SWOS United: dosmenus). */
#define PROJECT_DEFAULT_MODS    "dosmenus, display2020, scoreboard"   /* (display2020: OpenSWOS's since 2026-11-10, off
                                                                            *  there; its scoreboard part is the scoreboard mod's) */

/* OFF MODS: canonical mods this game turns OFF by default whatever their manifest's "default"
 * says -- still listed, and a player may turn them on ([mods] enabled). Slugs, separated by commas
 * or spaces. OpenSWOS: none. A fork lists the ones its game does without (SWOS United: the
 * challenge packs that are not SWOS 2020's). */
/* SWOS United: SWOS 2020's challenges alone, as its SELECT CATEGORY had them (Playaveli, 2026-11-12):
 * OpenSWOS's CLASSICS and petarku's COLLECTION are there for a player who turns them on. */
#define PROJECT_OFF_MODS        "challenge-classics, challenge-collection"

/* The menu shading mod's (dosmenus) look at start-up: 0 the PC's shading, 1 the Amiga's flat
 * menus (F5 switches it while the game runs). Both OpenSWOS and SWOS United: 1 (the owner, 2026-11-09). */
#define PROJECT_MENU_LOOK_AMIGA 1

#endif /* PROJECT_IDENTITY_H */
