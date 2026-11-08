/*
 * live_ticker.h -- an online match on Discord, as SWOS 2020 posts it: the discord2020 mod
 * (src/mod_discord2020.c, static in SWOS United).
 *
 * LIVE TICKERS: a line for each moment of the match -- the welcome, kick-off, goals, cards,
 * injuries, substitutions, the woodwork, corners, penalties, half time, 90/105/120 minutes, the
 * shoot-out, the end, a player who quit. LIVE SCORE BOARDS: the match's stats, once, at the end.
 * Channels by what the match was for: friendly, ranked, tournament (a league or cup match).
 *
 * The HOST posts (as SWOS 2020's does), except a quit, which the player who left posts. Posts go
 * through PROJECT_DISCORD_PROXY on a thread of their own, so the match never waits for one; a
 * test webhook ([mod.discord2020] webhook, or SWOS_DISCORD_WEBHOOK) takes every post instead.
 * Off with [mod.discord2020] post = off (SWOS.ini), and for a match through a matchmaker on this machine or the
 * local network (a test) unless a test webhook is set.
 *
 * A netplay match plays frames again after a rollback, so a moment is taken only once its frame is
 * CONFIRMED: the mod hears each as it happens (OpenSWOS's matchMoment and soundEvent hooks),
 * tagged with the frame being played (netFrame); a frame played again drops what it and the frames
 * after it had reported, and what a confirmed frame reported is posted. A goal a rollback takes
 * back is never posted. Nothing here is match state, and nothing changes how a match plays.
 */
#ifndef LIVE_TICKER_H
#define LIVE_TICKER_H

#include "swos_types.h"

#ifdef SWOS_SDL_BUILD

/* Who played whom, set by the lobby at kick-off. */
typedef struct {
    char    name[2][24];        /* player 1 (the host, the top team, team 1), player 2 */
    char    country[2][8];      /* ISO2 ("DE"), a home nation's "GB-ENG", or "" */
    char    location[2][32];    /* the country's name, or "" */
    char    team[2][24];
    char    tactic[2][24];
    int32_t mode;               /* LOBBY_MODE_* */
    char    tournament[64];     /* a league or cup match's, or "" */
    int32_t style;              /* GAMEPLAY_* */
    char    teamDb[16];         /* "96/97" */
    int32_t legs, leg;          /* a cup tie's (LobbyRules) */
    int32_t firstLeg1, firstLeg2;
    int32_t host;               /* this machine opened the game */
    int32_t localPlayer;        /* 1 or 2 */
} LiveTickerMatch;

/* The lobby, around a live match: Begin before kick-off (the welcome), End after it -- how it
 * ended, and NetplayMatch_LiveOutcome's line (the match is over, every frame played). */
void LiveTicker_Begin(const LiveTickerMatch *match);
void LiveTicker_End(int32_t finished, const char *why);

#endif

#endif /* LIVE_TICKER_H */
