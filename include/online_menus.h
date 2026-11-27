/*
 * online_menus.h -- SWOS 2020's online-play menus in OpenSWOS (SDL builds).
 * See src/online_menus.c and docs/MENUS.md section 10.
 */
#ifndef ONLINE_MENUS_H
#define ONLINE_MENUS_H

#include "swos_types.h"

/* Register the menus' handlers with the JSON loader. Idempotent. */
void    OnlineMenus_Init(void);

/* Show a JSON-only menu by its loader name, e.g. "swos2020/logInMenu". */
void    OnlineMenus_Show(const char *name);

/* SWOS 2020's "SERVER LOGIN": the same logInMenu as MULTIPLAYER's, the account checked and the
 * menu closed -- no matchmaking. For
 *   LOGIN_FOR_SPEEDRUN  SPEEDRUN (challengeGame0Extras_OnSelect): PLAY OFFLINE and LATEST RUNS
 *   LOGIN_FOR_GALORE    SWOS GALORE's RATE / SHARE (selectLatestUploads, selectShareHighlights):
 *                       NOTICE (LOG OUT when logged in) and MONTHLY CHART
 *   LOGIN_FOR_CHALLENGES  CHALLENGES (scenarioModeLogIn): NOTICE (a log-in is required; LOG OUT
 *                       when logged in) and ABOUT CHALLENGES -- no PLAY OFFLINE
 * `as`: who is logged in this session already ("" or NULL nobody), whose name LOGIN takes at once;
 * `news`: what the lower button opens (NULL: SPEEDRUN's LATEST RUNS; CHALLENGES ignores it). 0
 * logged in, 1 PLAY OFFLINE, -1 left (EXIT, LOG OUT). A project without accounts: 1, no menu. */
enum { LOGIN_FOR_SPEEDRUN = 1, LOGIN_FOR_GALORE = 2, LOGIN_FOR_CHALLENGES = 3 };
int32_t OnlineMenus_ServerLogIn(int32_t purpose, const char *as, void (*news)(void));

/* --menu=<name>: remember a menu for OnlineMenus_ShowStartMenu to open once, when
 * the front end first starts. ShowStartMenu returns 1 when it showed one (and so the
 * main menu must be put back, as every main-menu button does: CommonMenuExit). */
void    OnlineMenus_SetStartMenu(const char *name);
int32_t OnlineMenus_ShowStartMenu(void);
/* --online, and the online client (make ONLINE_CLIENT=1): start at the multiplayer log-in;
 * quitAfter, leaving it quits the game (FrontEndMainLoop asks QuitAfterStartMenu). */
void    OnlineMenus_StartOnline(int32_t quitAfter);
int32_t OnlineMenus_QuitAfterStartMenu(void);

/* --menu-selftest: load, unpack and check every imported menu, report how many
 * of their handlers are ported. 0 on success. */
int32_t OnlineMenus_SelfTest(void);

/* The matchmaking server has paired us with an opponent: leave the lobby and
 * connect to them directly (docs/NETPLAY.md section 7). Called from the
 * matchmaking menu's draw when MmClient_StartInfo() turns up. */
void        OnlineMenus_StartMatchFromMatchmaking(void);

/* The link is connected and the input delay agreed: open the lobby as `player` (1 the
 * host, 2 the joiner) and play the match set up there. Returns when it has been
 * played or someone left, with the link closed. Both ways in use it -- matchmaking
 * and the direct-IP menu. */
void        OnlineMenus_EnterLobby(int32_t player, const char *name);

/* --netplay-lobby=host|join: meet another instance on this machine and open the lobby
 * as the front end starts. Non-zero for an unknown role. */
int32_t     OnlineMenus_SetLoopbackLobby(const char *role);

/* --matchmaker-menu[=name]: log in to the matchmaking server as `name` and open
 * the matchmaking menu at startup, so the live lobby can be looked at without
 * navigating to it (and without gdb, which cannot hold the connection). */
void        OnlineMenus_SetAutoLogin(const char *name);
const char *OnlineMenus_MatchStatus(void);

/* Over the picture, each frame (platform_sdl.c draw_overlays): the lobby's chat in the chat
 * font, when the lobby is the menu on screen. */
void OnlineMenus_DrawOverlay(void);

#endif /* ONLINE_MENUS_H */
