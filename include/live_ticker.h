/*
 * live_ticker.h -- an online match on Discord, as SWOS 2020 posts it (src/live_ticker.c).
 *
 * LIVE TICKERS: a line for each moment of the match -- the welcome, kick-off, goals, cards,
 * injuries, substitutions, the woodwork, corners, penalties, half time, 90/105/120 minutes, the
 * shoot-out, the end, a player who quit. LIVE SCORE BOARDS: the match's stats, once, at the end.
 * Channels by what the match was for: friendly, ranked, tournament (a league or cup match).
 *
 * The HOST posts (as SWOS 2020's does), except a quit, which the player who left posts. Posts go
 * through PROJECT_DISCORD_PROXY on a thread of their own, so the match never waits for one; a
 * test webhook ([netplay] discord_webhook, or SWOS_DISCORD_WEBHOOK) takes every post instead.
 * Off with [netplay] discord = false, and for a match through a matchmaker on this machine or the
 * local network (a test) unless a test webhook is set.
 *
 * A netplay match plays frames again after a rollback, so a moment is taken only once its frame is
 * CONFIRMED: the match code reports each as it happens (LIVE_TICKER, and Audio_DosEvent's DOS
 * events), tagged with the frame being played; a frame played again drops what it and the frames
 * after it had reported, and what a confirmed frame reported is posted. A goal a rollback takes
 * back is never posted. Nothing here is match state, and nothing changes how a match plays.
 */
#ifndef LIVE_TICKER_H
#define LIVE_TICKER_H

#include "swos_types.h"

/* What the match code reports by name (the rest come from the DOS sound events). */
enum {
    LIVE_TICKER_INJURY = 1,     /* team, the player's PlayerInfo shortName, arg the severity 1..7 */
    LIVE_TICKER_SUB    = 2,     /* team, the player coming on, the one going off */
};

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

/* [netplay] discord and discord_webhook (main.c, from SWOS.ini). */
void LiveTicker_Configure(int32_t on, const char *testWebhook);

/* The lobby, around a live match: Begin before kick-off (the welcome), End after it -- how it
 * ended, and NetplayMatch_LiveOutcome's line (the match is over, every frame played). */
void LiveTicker_Begin(const LiveTickerMatch *match);
void LiveTicker_End(int32_t finished, const char *why);

/* src/netplay_match.c: a frame is about to be played (again, after a rollback), and every frame up
 * to `frame` is confirmed. */
void LiveTicker_FrameBegin(int32_t frame);
void LiveTicker_Confirmed(int32_t frame);

/* The match code (LIVE_TICKER) and Audio_DosEvent. */
void LiveTicker_Report(int32_t kind, int32_t team, const int8_t *name1, const int8_t *name2, int32_t arg);
void LiveTicker_DosEvent(uint8_t event, uint16_t arg);
/* A TeamGame's team, 1 or 2 (a substitution's). */
int32_t LiveTicker_TeamOf(const void *teamGame);

#define LIVE_TICKER(kind, team, name1, name2, arg) LiveTicker_Report((kind), (team), (name1), (name2), (arg))

#else
#define LIVE_TICKER(kind, team, name1, name2, arg) ((void)0)
#endif

#endif /* LIVE_TICKER_H */
