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
 * issues 1-3). [netplay] account_site = swosunited (or SWOS_UNITED_SITE=swosunited) uses it for
 * the log-in, the rankings and the leagues; sensiblesoccer, the default, keeps the old site.
 * Ranked and league results still go to sensiblesoccer.de (the account ids are the same). */
#define PROJECT_ACCOUNT_API     "https://swos.swosunited.com/api/"

/* The Discord posts (src/live_ticker.c): SWOS 2020's relay on sensiblesoccer.de, which keeps the
 * webhooks and posts to its server's LIVE TICKERS and LIVE SCORE BOARDS channels; the game sends a
 * channel name ("live_ticker_friendly", ...) and the text, never a webhook. "" posts nothing. */
#define PROJECT_DISCORD_PROXY   "https://sensiblesoccer.de/swos2020/discord_proxy.php"

/* SWOS 2020's online-menu entries for features this project will not have -- the log-in's NEWS,
 * MULTIPLAYER SETTINGS' commentary and chat rows, JOIN AMIGA SUPER LEAGUE, JOIN DISCORD, HELP
 * DESK (src/online_menus.c): 1 hides them, 0 keeps them for a fork that builds them. */
#define PROJECT_HIDE_SWOS2020_EXTRAS 0       /* (SWOS United builds them: kept, the owner 2026-11-02) */

/* MATCH MAKING as SWOS 2020 draws it (src/online_menus.c, its matchMakingMenu_OnDraw_New): the
 * seven-row table always drawn, MODE and JOIN / REQUESTED / ACCEPT, the red page arrows, HOST
 * MATCH / CANCEL, the host accepting joiners from the table. 0: OpenSWOS's own screen. */
#define PROJECT_SWOS2020_MATCHMAKING 1

#endif /* PROJECT_IDENTITY_H */
