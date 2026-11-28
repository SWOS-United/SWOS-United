#ifndef IC2026_H
#define IC2026_H

/* ic2026.c -- World 2026-27's first season (2026/27) plays the real 2026 FIFA Intercontinental Cup
 * (the owner, 2026-11-27): its six clubs, the two rounds played before the season (the African-Asian-
 * Pacific Cup play-off, Al-Ahli 1-0 Auckland, and Cup, Sundowns 2-1 Al-Ahli), then the Derby of the
 * Americas (Libertadores winner v Toluca), the Challenger Cup (v Sundowns) and the final (v PSG) in
 * December. The career club plays its own tie in slot 4 (as a Super Cup's); the others are decided
 * by strength. Each of the four is a trophy of its own: the career club's results are lines of the
 * season's record (season_extra.c). Only World 2026-27, only its first season: 96/97, Europe 1984-85
 * and every later season never run any of it (later seasons: the continental-cup engine's own
 * edition, world_conti.c). */

#include <stdint.h>

typedef struct Ic2026State {
    uint8_t  stage;             /* next round to decide: 0 Derby, 1 Challenger Cup, 2 final, 3 done */
    uint8_t  built;             /* the career club's tie in slot 4: 1 built, 0 none (or decided) */
    uint8_t  linesDone;         /* the career club's lines of the rounds before the season written */
    uint8_t  season;            /* 1 while running (World 2026-27's first season); 0 none */
    uint16_t teamA, teamB;      /* the built tie */
    uint16_t winner[3];         /* Derby, Challenger Cup, final (0: not decided) */
    uint8_t  week;              /* the built tie's week: its round's, or the week after the day just played */
} Ic2026State;

extern Ic2026State g_ic2026;

void     Ic2026_Reset(void);                  /* a new career / a save without it */
void     Ic2026_Step(uint16_t week);          /* a match day (career_teams.c, beside SuperCup_Step) */
void     Ic2026_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won);
int32_t  Ic2026_InSlot4(void);                /* 2 its tie under way, 1 decided and still there, 0 not ours */
void     Ic2026_RestoreSlot(void);            /* a loaded career: the tie under way back in slot 4 */
int32_t  Ic2026_RoundStringIndex(uint8_t compId);   /* -1: not ours */
uint16_t Ic2026_Winner(void);                 /* the 2026 winner (0: not decided) -- the next holder */

#endif
