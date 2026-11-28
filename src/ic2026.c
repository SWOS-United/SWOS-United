/*
 * ic2026.c -- the real 2026 FIFA Intercontinental Cup in World 2026-27's first season
 * (include/ic2026.h). Its tie in slot 4 is built as world_supercup.c builds a Super Cup's.
 */
#include "ic2026.h"

#include "comp_identity.h"
#include "diy_competition.h"
#include "endian_helpers.h"
#include "game_types.h"
#include "season_extra.h"
#include "career_teams.h"   /* AppendTeamsToSelectionFromTmdOrTeamFile */
#include "world.h"
#include "world_cups.h"

/* Prevent GCC from converting byte loops to memcpy/memset (no libc on the Amiga) */
#pragma GCC optimize("no-tree-loop-distribute-patterns")

extern uint16_t _selTeamNumber, _gameType;
extern uint8_t _isNationalSelector;
extern DIYCompetitionEx _diyCompetitionStart;
extern CompetitionSlotBuffer _diyCompetitionContextBuf, _primaryCompetitionBuf;
extern uint8_t *_diyCompetitionPtr;
extern uint32_t *_diyCompetitionTablePtr;
extern uint16_t _competitionSlotUpdatedFlag;
extern Playoffs _div1Playoffs;
extern uint16_t Rand(void);
extern uint8_t *GetCurrentSeasonPointer(void);
extern void Career_RetireSlotCalendarEntries(uint8_t slot);
extern void Career_SetCurrentCompetitionSlot(uint8_t *comp, uint16_t slot, uint16_t divisionIndex,
                                             uint16_t hasSelectedTeams);
extern void Career_LoadCompetitionSlotStateFromBuffer(uint8_t *slot);
extern void Career_StoreCompetitionSlotStateToBuffer(uint8_t *slot);
extern void DIY_BuildTeamSelectionPointerCache(void);
extern void DIY_UpdateCompetitionProgressOrAdvanceSlot(uint8_t *blk);
extern int32_t SuperCup_InSlot4(void);
extern int32_t CupD_InSlot4(void);

/* The clubs (team words, the world's files: PARIS-SG, SUNDOWNS, DEPORTIVO TOLUCA, AL-AHLI SFC,
 * AUCKLAND -- Auckland FC, in Australia's A-League, FLAMENGO). CONMEBOL's is the 2025 Copa Libertadores
 * winner, Flamengo, for every career starting in 2026/27 (the owner, 2026-11-27): the 2026 final (28
 * November 2026) is decided after a career's start, so it is not the one used. */
#define IC_PSG   0x0D00u
#define IC_SUN   0x4501u
#define IC_TOL   0x3C04u
#define IC_LIB   0x2E00u        /* FLAMENGO: the 2025 Copa Libertadores winner */
#define IC_AHLI  0x5902u
#define IC_AUCK  0x2C02u

/* The rounds still to play: week (eighths of a month from January: December 88..95), the round's
 * name (_cupRoundStringTable), the trophy line, each club's strength for a decision by strength. */
static const uint8_t  kWeek[3]  = { 89, 90, 91 };         /* (9, 12 and 16 December) */
static const uint8_t  kRound[3] = { 25, 26, 8 };          /* DERBY OF THE AMERICAS, CHALLENGER CUP, FINAL */
static const char *const kLine[3] = { "DERBY OF THE AMERICAS", "CHALLENGER CUP", "INTERCONTINENTAL CUP" };

static char kName0[] = "INTERCONTINENTAL CUP", kName1[] = "INTERC. CUP";

Ic2026State g_ic2026;
static CompetitionCup s_rec;

static int32_t on(void)
{
    return SWOS_WORLDS && World_Current() == WORLD_2627 && _gameType == 4 && !_isNationalSelector &&
           g_worldCups.seasons == 0;    /* (the first season: the 2026 edition) */
}

static uint32_t strength(uint16_t t)
{
    switch (t) {
    case IC_PSG: return 92;
    case IC_LIB: return 80;
    case IC_TOL: return 74;
    case IC_SUN: return 72;
    default:     return 70;
    }
}

void Ic2026_Reset(void)
{
    uint8_t *p = (uint8_t *)&g_ic2026;
    for (uint32_t i = 0; i < sizeof g_ic2026; i++)
        p[i] = 0;
}

static int32_t season_index(void)
{
    return SeasonExtra_Index((const SeasonInformations *)GetCurrentSeasonPointer());
}

static void line(const char *name, int8_t code)
{
    if (SeasonExtra_Freeze(season_index(), name, code))
        SWOS_DBG("[ic2026] season record: %s (%d)\n", name, (int)code);
}

/* The round's two clubs: the Derby, then each winner against the next club in. */
static void pair(int32_t r, uint16_t *a, uint16_t *b)
{
    switch (r) {
    case 0:  *a = IC_LIB; *b = IC_TOL; break;
    case 1:  *a = g_ic2026.winner[0]; *b = IC_SUN; break;
    default: *a = g_ic2026.winner[1]; *b = IC_PSG; break;
    }
}

/* The rounds before the season: the career club's lines (Sundowns won the African-Asian-Pacific Cup,
 * Al-Ahli lost it, Auckland lost its play-off). */
static void lines_before(void)
{
    if (g_ic2026.linesDone)
        return;
    g_ic2026.linesDone = 1;
    if (_selTeamNumber == IC_SUN)
        line("AFR.-ASIA-PAC. CUP", 1);
    else if (_selTeamNumber == IC_AHLI)
        line("AFR.-ASIA-PAC. CUP", -100);
    else if (_selTeamNumber == IC_AUCK)
        line("AFR.-ASIA-PAC. CUP", -111);
}

static void make_record(int32_t r)
{
    uint8_t *z = (uint8_t *)&s_rec;
    for (uint32_t i = 0; i < sizeof s_rec; i++)
        z[i] = 0;
    s_rec.header.comp = _div1Playoffs.comp;
    s_rec.header.comp.compId = (uint8_t)COMPID_ic2026;
    s_rec.header.comp.teamNumber = -1;
    s_rec.header.comp.isPlayoff = 0;
    s_rec.header.comp.reserved1 = 15;
    s_rec.header.comp.reserved2 = 21;
    s_rec.header.comp.seedReorderFlag = 1;
    s_rec.header.comp.beginMonth = (int8_t)g_ic2026.week;
    s_rec.header.comp.endMonth = (int8_t)g_ic2026.week;
    s_rec.header.teams = 2;
    s_rec.header.awayGoalsMode = 0;
    s_rec.header.subsAndPointsForWin = _div1Playoffs.subsAndMaxSubs;
    s_rec.header.roundStringBase = 1;
    s_rec.roundRuleFlags[0] = (int8_t)0x14;            /* (one match; extra time, penalties) */
    s_rec.name[0] = kName0;
    s_rec.name[1] = kName1;
    s_rec.teamSeedList[0].countryNumber = (uint8_t)(g_ic2026.teamA >> 8);
    s_rec.teamSeedList[0].teamOrdinal = (uint8_t)g_ic2026.teamA;
    s_rec.teamSeedList[1].countryNumber = (uint8_t)(g_ic2026.teamB >> 8);
    s_rec.teamSeedList[1].teamOrdinal = (uint8_t)g_ic2026.teamB;
}

static int32_t slot4_free(void)
{
    return !_diyCompetitionPtr || _diyCompetitionPtr == (uint8_t *)&s_rec ||
           SuperCup_InSlot4() == 1 || CupD_InSlot4() == 1;
}

/* Its week: the round's, but never before the week after the day just played (floor) -- built at a
 * fixed week, a tie came AFTER a later league game had been played and went into the calendar before
 * it, in the past, never played (Playa, 2026-11-27: PSG's final after Angers; as world_supercup.c's
 * floor). */
static void build(int32_t r, uint16_t a, uint16_t b, uint16_t floor)
{
    g_ic2026.teamA = a;
    g_ic2026.teamB = b;
    g_ic2026.built = 1;
    g_ic2026.week = (uint8_t)(kWeek[r] >= floor ? kWeek[r] : floor);
    make_record(r);
    _diyCompetitionTablePtr = (uint32_t *)0;
    Career_RetireSlotCalendarEntries(4);
    Career_SetCurrentCompetitionSlot((uint8_t *)&s_rec, 4, 0, 0);
    {
        uint8_t *ctx = (uint8_t *)&_diyCompetitionContextBuf;
        uint16_t ids[2];
        BE_WRITE_U16(&ids[0], a);
        BE_WRITE_U16(&ids[1], b);
        AppendTeamsToSelectionFromTmdOrTeamFile(((DIYCompetitionEx *)ctx)->cupTeamIds, ids, 2);
        Career_LoadCompetitionSlotStateFromBuffer(ctx);
        DIY_BuildTeamSelectionPointerCache();
        DIY_UpdateCompetitionProgressOrAdvanceSlot((uint8_t *)&_diyCompetitionStart);
        Career_StoreCompetitionSlotStateToBuffer(ctx);
        _competitionSlotUpdatedFlag = 1;
        _diyCompetitionContextBuf.tourn.header.slotStateA = _primaryCompetitionBuf.tourn.header.slotStateA;
        _diyCompetitionContextBuf.tourn.header.slotStateB = _primaryCompetitionBuf.tourn.header.slotStateB;
        _diyCompetitionContextBuf.tourn.header.slotStateC = _primaryCompetitionBuf.tourn.header.slotStateC;
    }
    SWOS_DBG("[ic2026] %s: %04X v %04X, week %u (the career club's)\n", kLine[r], (unsigned)a, (unsigned)b,
             (unsigned)g_ic2026.week);
}

/* A round decided: its winner, the career club's line. */
static void decided(int32_t r, uint16_t a, uint16_t b, uint16_t w)
{
    g_ic2026.winner[r] = w;
    g_ic2026.stage = (uint8_t)(r + 1);
    if (_selTeamNumber == a || _selTeamNumber == b)
        line(kLine[r], w == _selTeamNumber ? 1 : -100);
    SWOS_DBG("[ic2026] %s: %04X v %04X, won by %04X\n", kLine[r], (unsigned)a, (unsigned)b, (unsigned)w);
}

void Ic2026_Step(uint16_t week)
{
    if (!on() || g_ic2026.built)
        return;
    g_ic2026.season = 1;
    lines_before();
    while (g_ic2026.stage < 3 && week + 1 >= kWeek[g_ic2026.stage]) {
        const int32_t r = g_ic2026.stage;
        uint16_t a, b;
        pair(r, &a, &b);
        if (_selTeamNumber == a || _selTeamNumber == b) {
            if (slot4_free())
                build(r, a, b, (uint16_t)(week + 1));
            return;                     /* (its tie played, or waiting for the slot) */
        }
        const uint32_t sa = strength(a), sb = strength(b);
        const uint16_t w = ((uint32_t)Rand() * (sa + sb) >> 8) < sa ? a : b;
        decided(r, a, b, w);
    }
}

void Ic2026_TieDecided(uint16_t team1, uint16_t team2, int32_t team2Won)
{
    if (!g_ic2026.built || (uint8_t)_diyCompetitionStart.base.field_2E != COMPID_ic2026 ||
        !((team1 == g_ic2026.teamA && team2 == g_ic2026.teamB) || (team1 == g_ic2026.teamB && team2 == g_ic2026.teamA)))
        return;
    g_ic2026.built = 0;
    decided(g_ic2026.stage, g_ic2026.teamA, g_ic2026.teamB, team2Won ? team2 : team1);
}

int32_t Ic2026_InSlot4(void)
{
    return _diyCompetitionPtr == (uint8_t *)&s_rec ? (g_ic2026.built ? 2 : 1) : 0;
}

void Ic2026_RestoreSlot(void)
{
    if (!g_ic2026.built || g_ic2026.stage > 2)
        return;
    make_record(g_ic2026.stage);
    _diyCompetitionPtr = (uint8_t *)&s_rec;
    _diyCompetitionTablePtr = (uint32_t *)0;
}

int32_t Ic2026_RoundStringIndex(uint8_t compId)
{
    if (compId != COMPID_ic2026)
        return -1;
    return kRound[g_ic2026.stage < 3 ? g_ic2026.stage : 2];
}

uint16_t Ic2026_Winner(void)
{
    return g_ic2026.winner[2];
}
