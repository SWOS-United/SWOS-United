/*
 * nat_rank.c -- World 2026-27's national ranking (include/nat_rank.h).
 *
 * The base is the world's data (competitions/national_ranking.json, loaded on SDL by
 * DataJson_LoadNationalRanking); a career takes its own copy at first use and moves it after every
 * match between two national teams by FIFA's formula (the "SUM" method, 2018):
 *
 *     P = P_before + I x (W - W_e),   W_e = 1 / (10^(-dr/600) + 1)
 *
 * W 1 / 0.5 / 0 for a win / draw / loss, 0.75 and 0.5 for a shoot-out's winner and loser; dr the
 * points difference before the match; I the match's weight. A knockout defeat in a final tournament
 * costs no points. W_e comes from a table (the Amiga has no pow), every 20 points, interpolated.
 *
 * Without the world's data (the Amiga, or a world folder without the file) the base is the national
 * records' skill, so draws and picks still have an order.
 */
#include "nat_rank.h"

#include "comp_identity.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "national_teams.h"
#include "world.h"

#ifdef SWOS_SDL_BUILD
#include <stdio.h>
#endif

extern uint16_t Rand(void);
extern uint8_t  _nationalTeams[];
extern uint16_t _natTeamsRandLimit2;

NatRankState g_natRank;

static NatRankEntry s_base[NATRANK_MAX];
static uint16_t     s_baseN;

/* W_e x 10000 for dr = -800, -780 ... +800 (dr in ranking points). */
static const int16_t kExpected[81] = {
    444, 477, 513, 552, 594, 638, 685, 736, 790, 848, 909, 975, 1044, 1118, 1197, 1280, 1368, 1461,
    1560, 1663, 1773, 1887, 2008, 2134, 2265, 2403, 2545, 2694, 2847, 3006, 3170, 3339, 3511, 3688,
    3869, 4052, 4238, 4427, 4617, 4808, 5000, 5192, 5383, 5573, 5762, 5948, 6131, 6312, 6489, 6661,
    6830, 6994, 7153, 7306, 7455, 7597, 7735, 7866, 7992, 8113, 8227, 8337, 8440, 8539, 8632, 8720,
    8803, 8882, 8956, 9025, 9091, 9152, 9210, 9264, 9315, 9362, 9406, 9448, 9487, 9523, 9556 };

int32_t NatRank_On(void)
{
    return SWOS_WORLDS && World_Current() == WORLD_2627;
}

void NatRank_Reset(void)
{
    g_natRank.init = 0;
    g_natRank.n = 0;
}

void NatRank_ClearBase(void)
{
    s_baseN = 0;
}

void NatRank_AddBase(uint16_t team, int32_t points, uint8_t conf, uint8_t banned)
{
    if (s_baseN >= NATRANK_MAX)
        return;
    NatRankEntry *b = &s_base[s_baseN++];
    b->team = team;
    b->points = points;
    b->conf = conf;
    b->banned = banned;
}

/* No world data: the national records, their skill as points (1000 + 25 a skill point), the
 * confederation their file's continent. */
static void base_from_records(void)
{
    s_baseN = 0;
    for (uint16_t k = 0; k < _natTeamsRandLimit2 && s_baseN < NATRANK_MAX; k++) {
        const NationalTeamRecord *r = (const NationalTeamRecord *)_nationalTeams + k;
        if (r->countryNumber < 80 || r->countryNumber > 85)
            continue;
        NatRank_AddBase(PACK_BE_U16(r->countryNumber, r->teamOrdinal),
                        100000 + 2500 * (int32_t)BE_READ_U16(&r->skillOrPrice),
                        (uint8_t)(r->countryNumber - 80), 0);
    }
}

int32_t NatRank_Ensure(void)
{
    if (!NatRank_On())
        return 0;
    if (g_natRank.init)
        return 1;
    if (!s_baseN)
        base_from_records();
    g_natRank.n = s_baseN;
    for (uint16_t i = 0; i < s_baseN; i++)
        g_natRank.e[i] = s_base[i];
    g_natRank.init = 1;
    return 1;
}

static NatRankEntry *find(uint16_t team)
{
    for (uint16_t i = 0; i < g_natRank.n; i++)
        if (g_natRank.e[i].team == team)
            return &g_natRank.e[i];
    return 0;
}

void NatRank_RestoreEntry(uint16_t team, int32_t points)
{
    if (!NatRank_On())
        return;
    if (!g_natRank.init) {          /* (the save's list replaces the base's points, team by team) */
        if (!s_baseN)
            base_from_records();
        g_natRank.n = s_baseN;
        for (uint16_t i = 0; i < s_baseN; i++)
            g_natRank.e[i] = s_base[i];
        g_natRank.init = 1;
    }
    NatRankEntry *e = find(team);
    if (e)
        e->points = points;
}

const NatRankEntry *NatRank_Find(uint16_t team)
{
    return NatRank_Ensure() ? find(team) : 0;
}

int32_t NatRank_Points(uint16_t team)
{
    const NatRankEntry *e = NatRank_Find(team);
    return e ? e->points : -1;
}

int32_t NatRank_Position(uint16_t team)
{
    const NatRankEntry *e = NatRank_Find(team);
    if (!e)
        return 0;
    int32_t pos = 1;
    for (uint16_t i = 0; i < g_natRank.n; i++)
        if (g_natRank.e[i].points > e->points ||
            (g_natRank.e[i].points == e->points && g_natRank.e[i].team < e->team))
            pos++;
    return pos;
}

int32_t NatRank_Banned(uint16_t team)
{
    const NatRankEntry *e = NatRank_Find(team);
    return e ? e->banned : 0;
}

/* W_e x 10000 for a points difference in hundredths. */
static int32_t expected(int32_t drHundredths)
{
    int32_t dr = drHundredths;
    if (dr <= -80000)
        return kExpected[0];
    if (dr >= 80000)
        return kExpected[80];
    int32_t pos = dr + 80000;               /* 0 .. 160000, a step = 2000 */
    int32_t i = pos / 2000, f = pos % 2000;
    if (i >= 80)
        return kExpected[80];
    return kExpected[i] + (kExpected[i + 1] - kExpected[i]) * f / 2000;
}

/* A final tournament's: the World Cup, the Euro, a continental cup. */
static int32_t is_finals(uint8_t id)
{
    return id == COMPID_worldCup2 || id == COMPID_worldCup || comp_is_continental_cup(id);
}

static int32_t weight(uint8_t id, int32_t knockout, int32_t stageTeams)
{
    if (id == COMPID_worldCup2 || id == COMPID_worldCup)
        return (knockout && stageTeams <= 8) ? 60 : 50;
    if (comp_is_continental_cup(id))
        return (knockout && stageTeams <= 8) ? 40 : 35;
    if (comp_is_wc_qualifier(id) || comp_is_ec_qualifier(id))
        return 25;
    if (id == COMPID_friendly || id == COMPID_invitationTournament || id == COMPID_tour)
        return 10;
    return 0;
}

void NatRank_Result(uint8_t compId, uint16_t team1, uint16_t team2, uint16_t goals1, uint16_t goals2,
                    int32_t shootout, int32_t knockout, int32_t stageTeams)
{
    if ((team1 >> 8) < 80 || (team1 >> 8) > 85 || (team2 >> 8) < 80 || (team2 >> 8) > 85)
        return;
    const int32_t I = weight(compId, knockout, stageTeams);
    if (!I || !NatRank_Ensure())
        return;
    NatRankEntry *a = find(team1), *b = find(team2);
    if (!a || !b)
        return;
    int32_t wa;                             /* W x 10000, team1's */
    if (goals1 > goals2)
        wa = 10000;
    else if (goals1 < goals2)
        wa = 0;
    else if (shootout == 1)
        wa = 7500;
    else if (shootout == 2)
        wa = 5000;
    else
        wa = 5000;
    int32_t wb = (shootout && goals1 == goals2) ? (shootout == 1 ? 5000 : 7500) : 10000 - wa;
    const int32_t ea = expected(a->points - b->points), eb = 10000 - ea;
    int32_t da = I * (wa - ea) / 100, db = I * (wb - eb) / 100;    /* (hundredths: I x dW / 10000 x 100) */
    if (knockout && is_finals(compId)) {    /* (a knockout defeat in a final tournament costs nothing) */
        if (da < 0) da = 0;
        if (db < 0) db = 0;
    }
    a->points += da;
    b->points += db;
#ifdef SWOS_SDL_BUILD
    fprintf(stderr, "[nat-rank] comp %u: %04X %u-%u %04X%s -> %+d.%02d / %+d.%02d\n", (unsigned)compId,
            (unsigned)team1, (unsigned)goals1, (unsigned)goals2, (unsigned)team2,
            shootout ? (shootout == 1 ? " (pens: 1)" : " (pens: 2)") : "",
            (int)(da / 100), (int)((da < 0 ? -da : da) % 100), (int)(db / 100), (int)((db < 0 ? -db : db) % 100));
#endif
}

uint16_t NatRank_PickOpponent(uint16_t team, const uint8_t *used, int32_t n)
{
    if (!NatRank_Ensure())
        return 0xFFFF;
    const NatRankEntry *me = find(team);
    if (!me)
        return 0xFFFF;
    for (int32_t window = 15000; window <= 120000; window += 15000) {
        uint16_t cand[NATRANK_MAX];
        int32_t k = 0;
        for (uint16_t i = 0; i < g_natRank.n; i++) {
            const NatRankEntry *e = &g_natRank.e[i];
            if (e->team == team || e->banned)
                continue;
            int32_t d = e->points - me->points;
            if (d < -window || d > window)
                continue;
            int32_t taken = 0;
            for (int32_t j = 0; j < n && !taken; j++)
                taken = BE_READ_U16(used + 2 * j) == e->team;
            if (!taken)
                cand[k++] = e->team;
        }
        if (k >= 3 || window == 120000) {
            if (!k)
                return 0xFFFF;
            return cand[((uint32_t)Rand() * (uint32_t)k) >> 8];
        }
    }
    return 0xFFFF;
}
