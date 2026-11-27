#ifndef NAT_RANK_H
#define NAT_RANK_H

/* nat_rank.c -- World 2026-27's national ranking (the owner's spec, "World 2026-27 National
 * Competitions": phase 1). FIFA's men's ranking as the world's data gives it
 * (competitions/national_ranking.json, scripts/worldgen/nat_ranking.py), moved after every match
 * between two national teams by FIFA's own formula, P = P_before + I x (W - W_e), and saved with the
 * career ("national_ranking"). Only in World 2026-27: 96/97 and Europe 1984-85 never run any of it.
 * Both targets; the Amiga, which has no JSON world data, starts from the records' skill instead. */

#include <stdint.h>

#define NATRANK_CONF_NONE 0xFFu        /* confederation: 0 UEFA, 1 CAF, 2 CONMEBOL, 3 CONCACAF, 4 AFC, 5 OFC */

typedef struct NatRankEntry {
    uint16_t team;                     /* country << 8 | ordinal (the packed team word) */
    uint8_t  conf;
    uint8_t  banned;                   /* in no competition (Russia) */
    int32_t  points;                   /* hundredths of a ranking point */
} NatRankEntry;

#define NATRANK_MAX 256

typedef struct NatRankState {
    uint8_t      init;                 /* 0: not started this career (taken from the base at first use) */
    uint16_t     n;
    NatRankEntry e[NATRANK_MAX];
} NatRankState;

extern NatRankState g_natRank;         /* the career's (saved) */

int32_t  NatRank_On(void);             /* World 2026-27 is the current world */
void     NatRank_Reset(void);          /* a new career / a save without it: from the base at first use */
void     NatRank_ClearBase(void);      /* the world's data, as loaded (DataJson_LoadNationalRanking) */
void     NatRank_AddBase(uint16_t team, int32_t points, uint8_t conf, uint8_t banned);
int32_t  NatRank_Ensure(void);         /* the career's ranking ready; 0 when not in World 2026-27 */
void     NatRank_RestoreEntry(uint16_t team, int32_t points);  /* a save's (conf/banned from the base) */

const NatRankEntry *NatRank_Find(uint16_t team);   /* 0: not a ranked national team */
int32_t  NatRank_Points(uint16_t team);            /* hundredths; -1 unranked */
int32_t  NatRank_Position(uint16_t team);          /* 1 = best; 0 unranked */
int32_t  NatRank_Banned(uint16_t team);

/* A match between two national teams (both 80..85) has been decided: compId the competition's,
 * shootout 0 none / 1 team1 won it / 2 team2 won it, knockout nonzero in a knockout tie,
 * stageTeams the teams left in that stage (QF on = 8 or fewer). Anything else is ignored. */
void NatRank_Result(uint8_t compId, uint16_t team1, uint16_t team2, uint16_t goals1, uint16_t goals2,
                    int32_t shootout, int32_t knockout, int32_t stageTeams);

/* A friendly's / invitation's opponent: a national team ranked near `team` (by points), not banned,
 * not in `used` (n of them, packed words in big-endian byte order, as the cups' lists hold them);
 * 0xFFFF: none (the caller keeps its own pick). Random by the game's generator. */
uint16_t NatRank_PickOpponent(uint16_t team, const uint8_t *used, int32_t n);

#endif
