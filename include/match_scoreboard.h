/*
 * match_scoreboard.h -- SWOS 2020's scoreboard (its --new-scoreboard=1, scores mode 4): the minute,
 * both sides' kits and the score in a darkened box at the top left of the match, from the kick-off to
 * the final whistle (src/match_scoreboard.c). SDL builds.
 *
 * It is drawn on the picture as it goes to the screen, never into the frontbuffer: nothing the match
 * keeps, rolls back, records or hashes changes, and a frame netplay plays again is not drawn at all.
 */
#ifndef MATCH_SCOREBOARD_H
#define MATCH_SCOREBOARD_H

#include "swos_types.h"

#ifdef SWOS_SDL_BUILD
void    MatchScoreboard_SetEnabled(int32_t on);         /* [video] scoreboard */
/* Whether it is on screen now: a match (not training) between its kick-off and its final whistle,
 * and neither a replay nor the statistics showing. The game's own clock is not drawn then. */
int32_t MatchScoreboard_Showing(void);
/* Whether the game's own clock is left out: SWOS 2020 moves it off the screen for the whole match
 * while its scoreboard is on, so the "90 mins." of the final whistle does not take the board's place. */
int32_t MatchScoreboard_HidesClock(void);
/* Its box on the game's screen. */
void    MatchScoreboard_Rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h);
/* Draws it into an ARGB picture the game's size: `overlay` 1 for a layer drawn over the picture
 * (the box is then half-transparent black), 0 for the picture itself (the box darkens it). */
void    MatchScoreboard_Draw(uint32_t *argb, int32_t pitchPixels, int32_t originX, int32_t originY, int32_t overlay);

/* The result's band, as SWOS 2020 draws it (match_scoreboard.c). DrawQueuedSprites: SpritesBegin
 * before its sprites, and Sprite for each -- 1 to leave a grid sprite out (its rows noted), and
 * the team names' x moved out. The present, for the frame buffer it shows: BandRect (its box, 0 if
 * none), DrawBand (darkens the picture there, the text left alone), DrawBandKits (the kits). */
struct Sprite;
void    MatchScoreboard_SpritesBegin(void);
void    MatchScoreboard_KeepFresh(void);          /* the match is paused: what it drew stays */
int32_t MatchScoreboard_Sprite(const struct Sprite *spr, int16_t *x, int16_t y, int32_t h);
int32_t MatchScoreboard_BandRect(uintptr_t fb, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
void    MatchScoreboard_DrawBand(uint32_t *argb, int32_t pitchPixels, uintptr_t fb, const uint8_t *fb8);
void    MatchScoreboard_DrawBandKits(uint32_t *argb, int32_t pitchPixels, uintptr_t fb, int32_t originX, int32_t originY);
#endif

#endif
