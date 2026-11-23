/*
 * career_seasons.c -- SWOS United's two careers (Playa, 2026-11-19): the main menu's CLASSIC CAREER
 * YEARS (96/97's world, in the season SEASON START chose) and WORLD 26/27 CAREER (World 2026-27),
 * and the career screen's SEASON START.
 *
 * The seasons are SWOS 2020's team database DLC as mods (docs/MODS.md §10.4) in seasons/ beside
 * the game (include/mods.h MODS_SEASONS_DIR; scripts/make_seasons_package.py, installed with the
 * game), each mod.json naming its "season_label" (1994/95 v0.9, 2019/20 WINTER ...) and
 * "season_sort". SEASON START's first choice is 96/97's own data, "1996/97".
 *
 * The world and season played are SWOS.ini's [general] world and season (main.c mods_start: a
 * season only in 96/97's world), and a career that needs others changes them while the game runs
 * (src/world_switch.c, WorldSwitch_To: no restart) -- a contest that is on asked about first, as
 * the game asks before any new one (CheckContinueAbortPrompt).
 *
 * SEASON START is entries 14 / 15 of the career screen, under CAREER YEARS: hidden on the Amiga
 * (its world is its drawer's) and in another world than 96/97. Its choice is taken at CHOOSE
 * TEAM: the season changed then, if it is not the one played, and the career starts in it.
 * INFO beside it (entry 16) shows the chosen season's readme.txt -- its authors' notes, from the
 * DLC -- a page at a time (MenuDialog_ShowText); 96/97's own is seasons/ORIGINAL-9697.TXT.
 */
#include "game_types.h"
#include "career_params.h"
#include "menu_data.h"
#include "menu_system.h"

char _aSeasonStart[] = "SEASON START";
char _aSeasonInfo[] = "INFO";
char _seasonStartText[24] = "1996/97";

#ifdef SWOS_SDL_BUILD

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mjson.h"
#include "mods.h"
#include "world.h"
#include "platform_sdl.h"
#include "swos_settings.h"
#include "menu_dialog.h"
#include "assets_check.h"
#include "swos_io.h"
#include "project_identity.h"

#include <dirent.h>
#include <sys/stat.h>

extern void CareerMenu(void);
extern PlatformInitOptions *g_arcadeOpts;
extern const char *g_arcadeIniPath;

enum { kMaxSeasons = 64, kSeasonEntries = 14 };   /* (SEASON START: entries 14 label, 15 toggle, 16 INFO) */

typedef struct { char slug[MODS_ID_MAX], label[24], sort[16]; } Season;
#define ORIGINAL_README MODS_SEASONS_DIR "/ORIGINAL-9697.TXT"     /* (96/97's own: make_seasons_package.py) */
static Season  s_seasons[kMaxSeasons];
static int32_t s_count = -1;            /* (-1: not scanned yet) */
static int32_t s_pick;                  /* SEASON START's choice: an index of s_seasons */

static char *slurp(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = n > 0 && n < 1 << 20 ? (char *)malloc((size_t)n + 1) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) {
        free(b);
        b = NULL;
    }
    fclose(f);
    if (b) {
        b[n] = 0;
        *len = (int)n;
    }
    return b;
}

static int by_sort(const void *a, const void *b)
{
    const Season *x = (const Season *)a, *y = (const Season *)b;
    const int c = strcmp(x->sort, y->sort);
    return c ? c : strcmp(x->label, y->label);
}

/* The seasons: 96/97's own first, then every folder of seasons/ with a "season_label". */
static void scan(void)
{
    if (s_count >= 0)
        return;
    memset(s_seasons, 0, sizeof s_seasons);
    snprintf(s_seasons[0].label, sizeof s_seasons[0].label, "1996/97");
    snprintf(s_seasons[0].sort, sizeof s_seasons[0].sort, "1996-00");
    s_count = 1;
    DIR *d = opendir(MODS_SEASONS_DIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && s_count < kMaxSeasons) {
            char path[1200];
            int len = 0;
            if (e->d_name[0] == '.' || strlen(e->d_name) >= MODS_ID_MAX)
                continue;
            snprintf(path, sizeof path, "%s/%.31s/mod.json", MODS_SEASONS_DIR, e->d_name);
            char *json = slurp(path, &len);
            if (!json)
                continue;
            Season *s = &s_seasons[s_count];
            if (mjson_get_string(json, len, "$.season_label", s->label, (int)sizeof s->label) > 0) {
                snprintf(s->slug, sizeof s->slug, "%.31s", e->d_name);
                if (mjson_get_string(json, len, "$.season_sort", s->sort, (int)sizeof s->sort) <= 0) {
                    memcpy(s->sort, s->label, sizeof s->sort - 1);
                    s->sort[sizeof s->sort - 1] = 0;
                }
                s_count++;
            }
            free(json);
        }
        closedir(d);
    }
    qsort(s_seasons + 1, (size_t)(s_count - 1), sizeof s_seasons[0], by_sort);
    fprintf(stderr, "[seasons] %d in %s/ (and 96/97's own)\n", (int)s_count - 1, MODS_SEASONS_DIR);
}

/* The season this run plays: the season mod that is on, else 96/97's own (index 0). */
static int32_t running(void)
{
    scan();
    for (int32_t i = 1; i < s_count; i++)
        if (Mods_IsOn(s_seasons[i].slug))
            return i;
    return 0;
}

static int32_t index_of(const char *slug)
{
    scan();
    for (int32_t i = 1; i < s_count; i++)
        if (slug && strcmp(s_seasons[i].slug, slug) == 0)
            return i;
    return 0;
}

/* ---- the switch ------------------------------------------------------------------------- */

extern int32_t WorldSwitch_To(const char *world, const char *season);
extern int16_t CheckContinueAbortPrompt(uint8_t *header);
extern uint8_t _aCareer_1[];

/* The world and season a career needs, now (world_switch.c: no restart). 0 when they are played;
 * -1 when the player kept the contest that was on (CheckContinueAbortPrompt: CONTINUE). */
static int32_t switch_to(const char *world, const char *season)
{
    if (CheckContinueAbortPrompt(_aCareer_1) != 0)
        return -1;
    WorldSwitch_To(world, season);
    return 0;
}

/* The career screen's init (menus.c CareerMenuInit, after its defaults): SEASON START at the
 * season running. */
void CareerSeasons_CareerMenuInit(void)
{
    s_pick = running();
}

/* ---- the career screen's SEASON START ----------------------------------------------------- */

/* Its text, and the rows shown only in 96/97's world (else CAREER YEARS goes to CHOOSE TEAM). */
void SeasonStart_SetField(void)
{
    scan();
    if (s_pick < 0 || s_pick >= s_count)
        s_pick = 0;
    snprintf(_seasonStartText, sizeof _seasonStartText, "%s", s_seasons[s_pick].label);
    const int32_t shown = World_Current() == WORLD_9697;
    MenuEntry *label = CalcMenuEntryAddress(kSeasonEntries), *toggle = CalcMenuEntryAddress(kSeasonEntries + 1),
              *info = CalcMenuEntryAddress(kSeasonEntries + 2);
    label->isInvisible = toggle->isInvisible = info->isInvisible = (uint16_t)!shown;
    CalcMenuEntryAddress(13)->downEntry = (int8_t)(shown ? kSeasonEntries + 1 : 10);
    CalcMenuEntryAddress(10)->upEntry = (int8_t)(shown ? kSeasonEntries + 1 : 13);
}

/* Fire: the next season (as CAREER YEARS), round to 96/97's own after the last. */
void SeasonStartToggle(MenuEntry *entry)
{
    scan();
    s_pick = (s_pick + 1) % s_count;
    SeasonStart_SetField();
    CallDrawMenuItem(entry);
}

/* INFO: the chosen season's readme.txt (its authors' notes), else what its mod.json says. */
void SeasonInfo(void)
{
    char path[1200], title[48];
    int len = 0;
    scan();
    if (s_pick < 0 || s_pick >= s_count)
        s_pick = 0;
    const Season *s = &s_seasons[s_pick];
    snprintf(title, sizeof title, "%s", s->label);
    if (s_pick == 0)
        snprintf(path, sizeof path, "%s", ORIGINAL_README);
    else
        snprintf(path, sizeof path, "%s/%s/readme.txt", MODS_SEASONS_DIR, s->slug);
    char *text = slurp(path, &len);
    if (!text && s_pick > 0) {          /* (no readme: the database's name, authors and version) */
        char manifest[1200], name[128] = "", author[128] = "", version[32] = "", about[512] = "";
        int mlen = 0;
        snprintf(manifest, sizeof manifest, "%s/%s/mod.json", MODS_SEASONS_DIR, s->slug);
        char *json = slurp(manifest, &mlen);
        if (json) {
            mjson_get_string(json, mlen, "$.title", name, (int)sizeof name);
            mjson_get_string(json, mlen, "$.author", author, (int)sizeof author);
            mjson_get_string(json, mlen, "$.version", version, (int)sizeof version);
            mjson_get_string(json, mlen, "$.about", about, (int)sizeof about);
            free(json);
        }
        for (char *c = name; *c; c++)   /* (the DLC's folder names: 2019_20 for 2019/20) */
            if (*c == '_')
                *c = c > name && c[-1] >= '0' && c[-1] <= '9' && c[1] >= '0' && c[1] <= '9' ? '/' : ' ';
        text = (char *)malloc(1024);
        if (text)
            snprintf(text, 1024, "%s\n\nBy %s\nVersion %s\n\n%s\n\nThis database came without notes.",
                     name[0] ? name : s->label, author[0] ? author : "unknown", version[0] ? version : "1.0", about);
    }
    if (!text) {
        static const char *const ok[] = { "OK" };
        MenuDialog_Show(title, "This season has no notes.", ok, 1, 0);
        return;
    }
    MenuDialog_ShowText(title, text);
    free(text);
}

/* CHOOSE TEAM (career_init.c ChooseTeamAndStartCareer, first): the season SEASON START chose, now
 * when it is not the one played (world_switch.c: no restart). 0: the career goes on. */
int32_t CareerSeasons_StartOther(void)
{
    if (World_Current() != WORLD_9697 || s_pick == running() || s_pick < 0 || s_pick >= s_count)
        return 0;
    WorldSwitch_To("9697", s_seasons[s_pick].slug);
    return 0;
}

/* ---- the main menu's two careers ---------------------------------------------------------- */

/* CLASSIC CAREER: 96/97's world, in the season SWOS.ini names (SEASON START's), then the career
 * screen. */
void CareerSeasons_Classic(void)
{
    if (switch_to("9697", g_arcadeOpts ? g_arcadeOpts->season : "") == 0)
        CareerMenu();
}

/* WORLD 26/27 CAREER: World 2026-27 -- installed (its mod, by OpenSWOS Setup), else said. */
void CareerSeasons_World(void)
{
    char path[1024];
    FILE *f = World_Current() == WORLD_2627 ? NULL
              : fopen(swos_data_path("data_json/worlds/2627/WORLD.ini", path, (uint32_t)sizeof path), "rb");
    if (World_Current() != WORLD_2627 && !f) {
        static const char *const buttons[] = { "LAUNCH SETUP", "OK" };
        if (MenuDialog_Show("World 2026-27 is not installed",
                            "Its data is not installed. Download the World 2026-27 package, then add it in "
                            PROJECT_SETUP_NAME " and click Install.", buttons, 2, 1) == 0)
            AssetsCheck_LaunchSetup();
        return;
    }
    if (f)
        fclose(f);
    if (switch_to("2627", NULL) == 0)
        CareerMenu();
}

/* The season a career save is made in (save_format_json.c "season"): its label, "" for 96/97's own
 * (or another world). */
const char *CareerSeasons_RunningSlug(void)
{
    const int32_t i = World_Current() == WORLD_9697 ? running() : 0;
    return i ? s_seasons[i].slug : "";
}
const char *CareerSeasons_LabelOf(const char *slug)
{
    const int32_t i = index_of(slug);
    return slug && slug[0] && !i ? slug : s_seasons[i].label;
}

#else   /* the Amiga: 96/97 alone, SEASON START hidden (career_init.c) */

void SeasonStart_SetField(void) {}
void SeasonStartToggle(MenuEntry *entry) { (void)entry; }
void SeasonInfo(void) {}

#endif
