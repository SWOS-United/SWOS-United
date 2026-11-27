/*
 * world_switch.c -- another world or season while the game runs, no restart (SWOS United, Playa
 * 2026-11-19: CLASSIC CAREER, WORLD 26/27 CAREER and SEASON START, src/career_seasons.c).
 *
 * What a world and a season change is installed at the start, by FrontEndPreLoopSetup
 * (frontend.c) over the compiled data: the team numbering, the countries and competition tables,
 * league records written over 96/97's in place, the competition registry, the career contests,
 * the region tables, the odd divisions' fixtures, the nationality codes -- then the competitions
 * from JSON (a world's records over 96/97's by id), its cups, national teams, a team database's
 * leagues, the league tweaks. The mods (a season is one), the translations (its names) and the
 * country names come before.
 *
 * So the compiled data is kept as it was before any of that (Pristine_Keep: each block its
 * original bytes, once, before its first write -- at the start for what is known, and by an
 * installer just before it writes into a block it finds), and a switch puts it all back
 * (Pristine_RestoreAll), resets each installer's own state, and runs the start's installs again in
 * their order for the new world and season. A career started after it is the career a fresh start
 * of that world and season would play: `SWOS_TEST_SWITCH=2627,9697` runs the switches before the
 * golden careers (scripts/career_golden_test.sh), whose saves must equal the record.
 * SDL builds.
 */
#include "game_types.h"
#include "world.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "competition_data.h"
#include "career_params.h"
#include "mods.h"
#include "platform_sdl.h"
#include <SDL.h>              /* SDL_GetTicks: how long a switch takes */
#include "swos_settings.h"
#include "team_pool.h"
#include "league_phase.h"       /* LeaguePhase_SetModEurope */

/* ---- the original bytes ------------------------------------------------------------------- */

typedef struct { void *p; uint32_t n; uint8_t *copy; } Kept;
enum { kMaxKept = 2048 };
static Kept    s_kept[kMaxKept];
static int32_t s_nKept;

void Pristine_Keep(void *p, uint32_t n)
{
    if (!p || !n)
        return;
    for (int32_t i = 0; i < s_nKept; i++)
        if (s_kept[i].p == p && s_kept[i].n >= n)
            return;
    if (s_nKept == kMaxKept) {
        fprintf(stderr, "[switch] more than %d blocks kept: a switch would leave the rest as they are\n", kMaxKept);
        return;
    }
    uint8_t *c = (uint8_t *)malloc(n);
    if (!c)
        return;
    memcpy(c, p, n);
    s_kept[s_nKept].p = p;
    s_kept[s_nKept].n = n;
    s_kept[s_nKept].copy = c;
    s_nKept++;
}

static void restore_all(void)
{
    for (int32_t i = s_nKept - 1; i >= 0; i--)
        memcpy(s_kept[i].p, s_kept[i].copy, s_kept[i].n);
}

/* A competition record's size by its type (0 league, 1 cup or play-offs, 2 tournament). */
static uint32_t record_size(const Competition *c)
{
    switch (c->type) {
    case 0: return (uint32_t)sizeof(CompetitionLeague);
    case 1: return (uint32_t)(c->isPlayoff ? sizeof(Playoffs) : sizeof(CompetitionCup));
    case 2: return (uint32_t)sizeof(CompetitionTournament);
    default: return 0;
    }
}

extern uint16_t _teamsCountryNumbers[256];
extern void    *_countriesTable[262];
extern void    *_competitionsTable[256];
extern uint8_t  _shortCountryNames[NATIONALITY_CODES * 3];
extern const uint8_t *_fixtureSchedulePtrTable[23];
extern QualifyingGroup _worldCupQualifyingGroups, _ecQualifyingGroups, _customsTable, _worldTable,
    _europeTable, _africaTable, _southAmericaTable, _northAmericaTable, _asiaTable, _oceaniaTable;
extern CompetitionTournament _euroCup, _worldCup, _worldCup2, _europeanChampionships,
    _europeanChampionships2, _copaAmerica, _africanNationsCup, _northAmericanCup, _asianCup;
extern CompetitionCup _cupWinnersCup, _uefaCup;

/* Each table's records (the compiled ones: [leagues, -2, cups, -1], a region's 80..85 too). */
static void keep_tables_records(void)
{
    for (int32_t c = 0; c < 256; c++) {
        Competition **t = (Competition **)_competitionsTable[c];
        if (!t)
            continue;
        for (int32_t k = 0; k < 16 && (intptr_t)t[k] != -1; k++) {
            const intptr_t v = (intptr_t)t[k];
            if (v > -0x1000 && v < 0x1000)
                continue;               /* (-2, a 0xFC marker, a sentinel) */
            const uint32_t n = record_size(t[k]);
            if (n)
                Pristine_Keep(t[k], n);
        }
    }
}

/* At the start, before FrontEndPreLoopSetup installs anything (frontend.c). */
void WorldSwitch_KeepStart(void)
{
    static int32_t done;
    if (done++)
        return;                         /* (the first front end's: what is compiled) */
    extern void Comp_KeepPristine(void);       /* (competition_data.c: the registry, the contests) */
    static QualifyingGroup *const regions[] = {
        &_worldCupQualifyingGroups, &_ecQualifyingGroups, &_customsTable, &_worldTable, &_europeTable,
        &_africaTable, &_southAmericaTable, &_northAmericaTable, &_asiaTable, &_oceaniaTable,
    };
    static void *const singles[][2] = {
        { &_euroCup, (void *)sizeof _euroCup }, { &_worldCup, (void *)sizeof _worldCup },
        { &_worldCup2, (void *)sizeof _worldCup2 }, { &_europeanChampionships, (void *)sizeof _europeanChampionships },
        { &_europeanChampionships2, (void *)sizeof _europeanChampionships2 },
        { &_copaAmerica, (void *)sizeof _copaAmerica }, { &_africanNationsCup, (void *)sizeof _africanNationsCup },
        { &_northAmericanCup, (void *)sizeof _northAmericanCup }, { &_asianCup, (void *)sizeof _asianCup },
        { &_cupWinnersCup, (void *)sizeof _cupWinnersCup }, { &_uefaCup, (void *)sizeof _uefaCup },
    };
    Pristine_Keep(_teamsCountryNumbers, sizeof _teamsCountryNumbers);
    Pristine_Keep(_countriesTable, sizeof _countriesTable);
    Pristine_Keep(_competitionsTable, sizeof _competitionsTable);
    Pristine_Keep(_shortCountryNames, sizeof _shortCountryNames);
    Pristine_Keep((void *)_fixtureSchedulePtrTable, sizeof _fixtureSchedulePtrTable);
    for (uint32_t i = 0; i < sizeof regions / sizeof regions[0]; i++)
        Pristine_Keep(regions[i], sizeof *regions[i]);
    for (uint32_t i = 0; i < sizeof singles / sizeof singles[0]; i++)
        Pristine_Keep(singles[i][0], (uint32_t)(uintptr_t)singles[i][1]);
    keep_tables_records();
    Comp_KeepPristine();
    fprintf(stderr, "[switch] %d blocks of the compiled data kept (a world or season changes without a restart)\n",
            (int)s_nKept);
}

/* SWOS_SWITCH_DIGEST=1: what the installs left, a digest a part (raw bytes: run with the address
 * space fixed, setarch -R, to compare a fresh start's with a switch's). */
static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *q = (const uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) { h ^= q[i]; h *= 16777619u; }
    return h;
}
void WorldSwitch_Digest(const char *when)
{
    extern CompetitionCareer _careerContests[];
    extern CareerParams _careerParams;
    if (!getenv("SWOS_SWITCH_DIGEST"))
        return;
    fprintf(stderr, "[digest] %s: numbers %08x countries %08x comps %08x nat %08x fixtures %08x params %08x contests %08x\n",
            when, (unsigned)fnv(2166136261u, _teamsCountryNumbers, sizeof _teamsCountryNumbers),
            (unsigned)fnv(2166136261u, _countriesTable, sizeof _countriesTable),
            (unsigned)fnv(2166136261u, _competitionsTable, sizeof _competitionsTable),
            (unsigned)fnv(2166136261u, _shortCountryNames, sizeof _shortCountryNames),
            (unsigned)fnv(2166136261u, _fixtureSchedulePtrTable, sizeof _fixtureSchedulePtrTable),
            (unsigned)fnv(2166136261u, &_careerParams, sizeof _careerParams),
            (unsigned)fnv(2166136261u, _careerContests, CAREER_CONTESTS_CAP * sizeof _careerContests[0]));
    for (int32_t c = 0; c < 256; c++) {
        Competition **t = (Competition **)_competitionsTable[c];
        uint32_t h = 2166136261u;
        if (!t)
            continue;
        for (int32_t k = 0; k < 16 && (intptr_t)t[k] != -1; k++) {
            const intptr_t v = (intptr_t)t[k];
            if (v > -0x1000 && v < 0x1000)
                continue;
            h = fnv(h, t[k], record_size(t[k]));
        }
        fprintf(stderr, "[digest] %s: country %d %08x\n", when, (int)c, (unsigned)h);
    }
    for (int32_t i = 0; i < s_nKept; i++)
        fprintf(stderr, "[digest] %s: kept %d (%u bytes) %08x\n", when, (int)i, (unsigned)s_kept[i].n,
                (unsigned)fnv(2166136261u, s_kept[i].p, s_kept[i].n));
}

/* ---- the switch --------------------------------------------------------------------------- */

extern PlatformInitOptions *g_arcadeOpts;
extern const char *g_arcadeIniPath;

/* The installs of the start (frontend.c FrontEndPreLoopSetup, its world and data part), in order. */
static void install_all(void)
{
    extern void DataJson_FitModTeamFiles(void);
    extern void DataJson_InstallCompetitionGraph(void);
    extern void WorldCups_Install(void);
    extern void DataJson_LoadCareerParams(void);
    extern void DataJson_LoadNationalTeams(void);
    extern void WorldNationals_Install(void);
    extern void TeamDb_Apply(void);
    extern void TeamDb_ApplyLeagueTweaks(void);
    extern void Career_InstallCopaLibertadoresPresetEntry(void);
    World_SnapshotBase();
    World_InstallBaked();
    DataJson_FitModTeamFiles();
    DataJson_InstallCompetitionGraph();
    WorldCups_Install();
    DataJson_LoadCareerParams();
    DataJson_LoadNationalTeams();
    { extern void DataJson_LoadNationalRanking(void); DataJson_LoadNationalRanking(); }
    WorldNationals_Install();
    TeamDb_Apply();
    TeamDb_ApplyLeagueTweaks();
    World_SnapshotWorld();
    Career_InstallCopaLibertadoresPresetEntry();
}

/* Each installer's own state back to before the start's install. */
static void reset_installers(void)
{
    extern void DataJson_ResetCompetitionGraph(void);
    extern void WorldCups_ResetTables(void);
    extern void FixtureRounds_Reset(void);
    extern void LeaguePhase_Reset(void);
    extern void TeamDb_Reset(void);
    DataJson_ResetCompetitionGraph();
    WorldCups_ResetTables();
    FixtureRounds_Reset();
    LeaguePhase_Reset();
    TeamDb_Reset();
}

static void reinstall(WorldId w);

/* The world `world` ("9697", "2627" ...) with the season `season` (a folder in seasons/, ""
 * none; only in 96/97's world), now: SWOS.ini's [general] world and season too. 0 when it is
 * played (already, or now), -1 when the world is unknown. Only between contests: the caller asks
 * first (CheckContinueAbortPrompt). */
int32_t WorldSwitch_To(const char *world, const char *season)
{
    extern void Mods_Reload(void);
    extern void Translations_Load(const char *lang);
    extern void Countries_Localize(void);
    extern const char *CareerSeasons_RunningSlug(void);
    extern uint8_t _lastLoadedExtNumber;
    extern uint16_t _worldOption;
    const int32_t w = World_FromTag(world && world[0] ? world : "9697");
    if (w < 0 || !g_arcadeOpts)
        return -1;
    const char *s = w == (int32_t)WORLD_9697 && season ? season : "";
    const int32_t same = w == (int32_t)World_Current() && strcmp(s, CareerSeasons_RunningSlug()) == 0;
    snprintf(g_arcadeOpts->world, sizeof g_arcadeOpts->world, "%s", World_TagOf((WorldId)w));
    if (w == (int32_t)WORLD_9697 && season)
        snprintf(g_arcadeOpts->season, sizeof g_arcadeOpts->season, "%s", season);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
    _worldOption = (uint16_t)w;
    if (same)
        return 0;

    fprintf(stderr, "[switch] %s%s%s -> %s%s%s\n", World_Tag(), CareerSeasons_RunningSlug()[0] ? " " : "",
            CareerSeasons_RunningSlug(), World_TagOf((WorldId)w), s[0] ? " " : "", s);
    reinstall((WorldId)w);
    return 0;
}

/* The start's installs again, for the world `w` and the mods mods_start (main.c) now takes. */
static void reinstall(WorldId w)
{
    extern void Mods_Reload(void);
    extern void Translations_Load(const char *lang);
    extern void Countries_Localize(void);
    extern const char *CareerSeasons_RunningSlug(void);
    extern uint8_t _lastLoadedExtNumber;
    const uint32_t t0 = SDL_GetTicks();
    restore_all();
    reset_installers();
    World_Set(World_TagOf(w));
    LeaguePhase_SetModEurope(NULL, NULL, 0, NULL);  /* (a mod's, set again by its init if it still applies) */
    Mods_Reload();                                  /* (the season's mod: main.c mods_start) */
    {   /* the files read once and kept, before anything reads them again: the next reader takes
         * the new season's -- its POOLPLYR.DAT (the foreign market's players: a 1999/00 career drew
         * 96/97's after a switch), its cups' .TMD (TeamDb_Apply below: 2024/25 drew 1994/95's) */
        extern uint16_t _poolplyrLoaded, _teamsLoaded;
        extern void AssetCache_ForgetData(void);
        _poolplyrLoaded = 0;
        _teamsLoaded = 0;
        _lastLoadedExtNumber = 0xFF;    /* (no team file of the world before cached) */
        AssetCache_ForgetData();        /* (every data/ file cached: asset_loader.c) */
    }
    Translations_Load(g_arcadeOpts->language);
    Countries_Localize();
    install_all();
    TeamPool_Reset();
    fprintf(stderr, "[switch] now %s%s%s (%u ms)\n", World_Tag(), CareerSeasons_RunningSlug()[0] ? " " : "",
            CareerSeasons_RunningSlug(), (unsigned)(SDL_GetTicks() - t0));
    WorldSwitch_Digest("switched");
}

/* MULTIPLAYER plays SWOS 96/97 as it comes, whatever is on offline (Playa, 2026-11-21: "it has to
 * work with 96/97 automatically, no matter what"). A world alone is swapped out by world_online.c;
 * a mod that is not for online play ("online": false: a team database, SEASON START's season) is
 * part of what the start installed, so it takes a switch: the mods without them and 96/97, as a
 * start with them off would be -- SWOS.ini untouched -- and, after online play, the world and the
 * mods of before, again as a start. 1 when it switched (WorldSwitch_OnlineEnd then switches back). */
static WorldId s_onlineBack;
static int32_t s_onlineSwitched;

int32_t WorldSwitch_OnlineBegin(void)
{
    extern const char *CareerSeasons_RunningSlug(void);
    if (s_onlineSwitched || !g_arcadeOpts || (!Mods_OfflineOnly(NULL, 0) && !CareerSeasons_RunningSlug()[0]))
        return 0;
    s_onlineBack = World_Current();
    Mods_SetOnlinePlay(1);
    fprintf(stderr, "[switch] online: 96/97 without the offline-only mods (back to %s after)\n",
            World_TagOf(s_onlineBack));
    reinstall(WORLD_9697);
    s_onlineSwitched = 1;
    return 1;
}

void WorldSwitch_OnlineEnd(void)
{
    if (!s_onlineSwitched)
        return;
    s_onlineSwitched = 0;
    Mods_SetOnlinePlay(0);
    fprintf(stderr, "[switch] online left: %s and its mods again\n", World_TagOf(s_onlineBack));
    reinstall(s_onlineBack);
}

/* SWOS_TEST_ONLINE_SWITCH=1 (with SWOS_SWITCH_DIGEST=1): into online play and out of it at once,
 * then quit -- the digest online must be a start's with no offline-only mod, the one after it the
 * start's again. */
void WorldSwitch_OnlineTest(void)
{
    if (!getenv("SWOS_TEST_ONLINE_SWITCH"))
        return;
    const int32_t switched = WorldSwitch_OnlineBegin();
    char mods[200];
    fprintf(stderr, "[switch] test online: switched %d, world %s, offline-only mods on: %d\n", (int)switched,
            World_Tag(), (int)Mods_OfflineOnly(mods, sizeof mods));
    WorldSwitch_OnlineEnd();
    fprintf(stderr, "[switch] test back: world %s, offline-only mods on: %d (%s)\n", World_Tag(),
            (int)Mods_OfflineOnly(mods, sizeof mods), mods);
    exit(0);
}

/* SWOS_TEST_SWITCH=<world[:season]>,...: those switches, in order, before a test runs (the golden
 * careers: src/career_autopilot.c) -- what it then plays must be what a fresh start would. */
void WorldSwitch_Test(void)
{
    const char *v = getenv("SWOS_TEST_SWITCH");
    if (!v || !v[0])
        return;
    char list[256];
    snprintf(list, sizeof list, "%s", v);
    for (char *tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
        char *colon = strchr(tok, ':');
        if (colon)
            *colon++ = 0;
        if (WorldSwitch_To(tok, colon ? colon : "") != 0)
            fprintf(stderr, "[switch] test: no world '%s'\n", tok);
    }
}

#else

void Pristine_Keep(void *p, uint32_t n) { (void)p; (void)n; }
void WorldSwitch_OnlineTest(void) {}

#endif
