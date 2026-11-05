/*
 * mod_menus2020.c -- SWOS United's menus2020 mod: SWOS 2020's main menu and the screens its
 * buttons open (docs/SWOS2020_MODS.md, "menus2020"). SDL builds; United only (src/fork_mods.inc).
 *
 * The menus are data, made from SWOS 2020's .mnu files by scripts/make_menus2020.py into
 * bin/mods/menus2020/data_json/menus/: swosMainMenu.json replaces the game's main menu through
 * the mod overlay while the mod is on, the rest are its own screens (menus2020/<menu>). Every
 * handler they name is one of these, "menus2020.<name>" (ModApi_RegisterHandlers).
 *
 * What the buttons do here, by what they say:
 *   COMPETE    COMPETITIONS: ONLINE (the game's MULTIPLAYER), DIY, PRESET, SEASON, CLASSIC CAREER,
 *              and SPEEDRUNS / CHALLENGES when OpenSWOS's speedrun / challenges mods are on
 *   PRACTICE   FRIENDLY MATCH; TEAM TRAINING, PENALTY SHOOTOUT, DEMO GAME (OpenSWOS's practice
 *              mod, when on); MODES -- the mods' entries (MODPOINT_MODES) -- when another has one
 *   REPLAYS    the game's HIGHLIGHTS
 *   OPTIONS    the game's OPTIONS
 *   EDITING    EDIT CUSTOM TEAMS, EDIT TACTICS, SAVE DISK FILING
 *   EXTRAS     EDIT CUSTOM TEAMS, EDIT TACTICS, INSTRUCTION MANUAL (LATEST NEWS, TIPS AND
 *              REVIEWS, STREAMS AND VIDEOS hidden: the ticker mod's)
 *   CONTINUE / SAVE / LOAD, QUIT, CREDITS
 *
 * Two rules of the game's menus shape this file:
 *   - Every main-menu handler of the game (FriendlyMenu, CareerMenu, ...) ends in
 *     CommonMenuExit (src/menus.c), which sets the CURRENT menu's entries 11..16 by ordinal.
 *     So this main menu keeps those ordinals' meanings (the generator orders it so), and a
 *     sub-screen never calls such a handler itself: it records the choice and closes, and the
 *     main-menu button that opened it runs it once the main menu is current again.
 *   - A menu shown again after a nested one is unpacked again from its bytes, without its
 *     onInit. So the main menu's layout is put right after every action (refresh), and the
 *     sub-screens never nest anything.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include <stdio.h>

#include "menu_system.h"
#include "menus.h"
#include "platform_sdl.h"       /* Platform_OpenUrl, Platform_RequestQuit */

#define SLUG "menus2020"

extern int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing, uint8_t *contText,
                                   uint8_t *abortTextStr, uint16_t defaultEntry);
extern void MultiplayerMenuSelected(void);
extern void CreditsMenuSelected(void);
extern void ModesMenuSelected(void);

static MenuEntry *entry(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void nav(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
}

static void show(int32_t ordinal, int32_t visible) { entry(ordinal)->isInvisible = visible ? 0 : 1; }

/* ======================================================================================
 * The main menu. Ordinals: scripts/make_menus2020.py MAIN_ORDER.
 * ====================================================================================== */
enum {
    Main_compete, Main_practice, Main_replays, Main_options, Main_editing, Main_extras,
    Main_quit,
    Main_continue = 11, Main_save, Main_load, Main_multiplayer, Main_credits, Main_modes,
};

static uint16_t s_loadX, s_loadY;       /* LOAD's place under CONTINUE, as unpacked */
static int32_t  s_loadKnown;

/* After CommonMenuExit (run by onInit and by every game handler): it shows CONTINUE / SAVE
 * only with a competition on, moving LOAD up to CONTINUE's row without one, and shows MODES
 * when a mod has an entry. Here: MULTIPLAYER and MODES stay hidden (COMPETE and PRACTICE
 * have them), LOAD is centred on CONTINUE's row when it is alone, and the arrows go where
 * the buttons are. */
static void main_refresh(void)
{
    MenuEntry *cont = entry(Main_continue), *load = entry(Main_load);
    const int32_t contest = !cont->isInvisible;

    show(Main_multiplayer, 0);
    show(Main_modes, 0);

    if (!s_loadKnown && contest) {      /* (CommonMenuExit has not moved it) */
        s_loadX = load->x;
        s_loadY = load->y;
        s_loadKnown = 1;
    }
    if (contest) {
        if (s_loadKnown) {
            load->x = s_loadX;
            load->y = s_loadY;
        }
        nav(Main_replays, -1, Main_extras, Main_practice, Main_continue);
        nav(Main_extras, Main_replays, -1, Main_editing, Main_continue);
        nav(Main_continue, -1, -1, Main_replays, Main_save);
        nav(Main_save, Main_quit, Main_load, Main_continue, -1);
        nav(Main_load, Main_save, Main_credits, Main_continue, -1);
        nav(Main_quit, -1, Main_save, Main_continue, -1);
        nav(Main_credits, Main_load, -1, Main_continue, -1);
    } else {
        if (!s_loadKnown) {             /* where it was, from SAVE's row (both 22 high) */
            s_loadX = load->x;
            s_loadY = entry(Main_save)->y;
            s_loadKnown = 1;
        }
        load->x = (uint16_t)(cont->x + (cont->dwWidth - load->dwWidth) / 2);
        load->y = cont->y;
        nav(Main_replays, -1, Main_extras, Main_practice, Main_load);
        nav(Main_extras, Main_replays, -1, Main_editing, Main_load);
        nav(Main_load, Main_quit, Main_credits, Main_replays, -1);
        nav(Main_quit, -1, Main_load, Main_replays, -1);
        nav(Main_credits, Main_load, -1, Main_extras, -1);
    }
}

static void mainInit(void)
{
    extern void SWOSMainMenuInit(void);
    SWOSMainMenuInit();                 /* the game's: CommonMenuExit, its test hook */
    main_refresh();
}

/* What a sub-screen chose, run by the main-menu button that opened it. */
static void (*s_pending)(void);

static void choose(void (*action)(void))
{
    s_pending = action;
    SetExitMenuFlag();
}

static void open_screen(const char *menu)
{
    s_pending = NULL;
    if (ModApi_ShowMenu(menu) == 0 && s_pending) {
        void (*action)(void) = s_pending;
        s_pending = NULL;
        action();
    }
    CommonMenuExit();
    main_refresh();
}

static void compete(void)  { open_screen(SLUG "/competitionMenu"); }
static void practice(void) { open_screen(SLUG "/practiceMenu"); }
static void editing(void)  { open_screen(SLUG "/editingMenu"); }
static void extras(void)   { open_screen(SLUG "/extrasMenu"); }

/* The game's own handlers, each ending in CommonMenuExit, and then this menu's layout. */
#define GAME_BUTTON(name, fn) static void name(void) { fn(); main_refresh(); }
GAME_BUTTON(replays, HighlightsMenu)
GAME_BUTTON(options, OptionsMenuSelected)
GAME_BUTTON(continueContest, StartContest)
GAME_BUTTON(saveContest, SaveContest)
GAME_BUTTON(loadContest, LoadOldCompetitionMenu)
GAME_BUTTON(credits, CreditsMenuSelected)
#undef GAME_BUTTON

/* QUIT (SWOS 2020's showQuitMenu): asked first, as closing the window is not. */
static void quit(void)
{
    static char header[] = "QUIT THE GAME?", none[] = "", yes[] = "QUIT", no[] = "CANCEL";
    if (DoContinueAbortMenu((uint8_t *)header, (uint8_t *)none, (uint8_t *)yes,
                            (uint8_t *)no, 1) == 0)
        Platform_RequestQuit();
    CommonMenuExit();
    main_refresh();
}

/* ======================================================================================
 * COMPETITIONS (swos-2020 competition.mnu). Ordinals: its entries, in order.
 * ====================================================================================== */
enum { Comp_header, Comp_online, Comp_speedruns, Comp_diy, Comp_challenges, Comp_preset,
       Comp_career, Comp_exit,
       Comp_season };                   /* (ours: the original main menu's SEASON) */

/* A row pair: SWOS 2020's two columns when the left one (a mod's) is there, else the right one
 * alone in the middle. Returns the ordinal arrows reach the row by. */
static int32_t comp_row(int32_t left, const char *slug, int32_t right, int32_t mid)
{
    const int32_t there = ModApi_FindEntry(MODPOINT_MODES, slug) >= 0;
    MenuEntry *r = entry(right);
    show(left, there);
    if (!there)
        r->x = (uint16_t)(mid - r->dwWidth / 2);
    return there ? left : right;
}

static void competitionInit(void)
{
    /* SPEEDRUNS and CHALLENGES are OpenSWOS's mods' (speedrun, challenges): shown when that
     * mod is on, beside DIY and PRESET as in SWOS 2020; without them DIY and PRESET move in
     * from the right-hand column to the middle. */
    const MenuEntry *online = entry(Comp_online);
    const int32_t mid = online->x + online->dwWidth / 2;
    const int32_t row2 = comp_row(Comp_speedruns, "speedrun", Comp_diy, mid);
    const int32_t row3 = comp_row(Comp_challenges, "challenges", Comp_preset, mid);
#ifdef SWOS_NETPLAY
    const int32_t top = Comp_online;
#else
    const int32_t top = -1;
    show(Comp_online, 0);              /* (no online play in this build) */
#endif
    nav(Comp_online, -1, -1, -1, row2);
    nav(Comp_speedruns, -1, Comp_diy, top, Comp_challenges);
    nav(Comp_diy, row2 == Comp_speedruns ? Comp_speedruns : -1, -1, top, Comp_preset);
    nav(Comp_challenges, -1, Comp_preset, row2, Comp_season);
    nav(Comp_preset, row3 == Comp_challenges ? Comp_challenges : -1, -1, Comp_diy, Comp_career);
    nav(Comp_season, -1, Comp_career, row3, Comp_exit);
    nav(Comp_career, Comp_season, -1, Comp_preset, Comp_exit);
    nav(Comp_exit, -1, -1, Comp_career, -1);
    SetCurrentEntry(Comp_diy);
}

/* A mod's MODES entry, run with the main menu current (as every choice here is). */
static const char *s_entrySlug;

static void run_entry(void)
{
    const int32_t i = ModApi_FindEntry(MODPOINT_MODES, s_entrySlug);
    if (i >= 0)
        ModApi_RunEntry(MODPOINT_MODES, i);
}

static void speedruns(void)  { s_entrySlug = "speedrun";   choose(run_entry); }
static void challenges(void) { s_entrySlug = "challenges"; choose(run_entry); }

static void online(void) { choose(MultiplayerMenuSelected); }
static void diy(void)    { choose(DIYCompetitionMenu); }
static void preset(void) { choose(PresetCompetitionMenu); }
static void career(void) { choose(CareerMenu); }
static void season(void) { choose(SeasonMenu); }

/* ======================================================================================
 * PRACTICE (swos-2020 practice.mnu): FRIENDLY MATCH; TEAM TRAINING, PENALTY SHOOTOUT and DEMO GAME
 * are OpenSWOS's practice mod's (practice.training / .shootout / .demo, named in the JSON), shown
 * while it is on; MODES (ours, the mods' entries) when a mod other than practice has one.
 * ====================================================================================== */
enum { Prac_header, Prac_friendly, Prac_training, Prac_shootout, Prac_demo, Prac_help, Prac_exit,
       Prac_modes };

static void practiceInit(void)
{
    const int32_t practice = ModApi_FindEntry(MODPOINT_MODES, "practice") >= 0;
    const int32_t modes = ModApi_EntryCount(MODPOINT_MODES) > (practice ? 1 : 0);
    const int32_t last = modes ? Prac_modes : practice ? Prac_demo : Prac_friendly;

    show(Prac_training, practice);
    show(Prac_shootout, practice);
    show(Prac_demo, practice);
    show(Prac_modes, modes);
    nav(Prac_friendly, -1, -1, -1, practice ? Prac_training : modes ? Prac_modes : Prac_exit);
    nav(Prac_demo, -1, -1, Prac_shootout, modes ? Prac_modes : Prac_exit);
    nav(Prac_modes, -1, -1, practice ? Prac_demo : Prac_friendly, Prac_exit);
    nav(Prac_exit, -1, -1, last, -1);
}

static void friendly(void) { choose(FriendlyMenu); }
static void modes(void)    { choose(ModesMenuSelected); }

/* ======================================================================================
 * EDITING (editing.mnu) and EXTRAS (extrasEx.mnu)
 * ====================================================================================== */
enum { Extras_header, Extras_news, Extras_tips, Extras_streams, Extras_editTeams,
       Extras_editTactics, Extras_manual, Extras_help, Extras_exit };

static void extrasInit(void)
{
    /* The left-hand column (the ticker's) is hidden: the right-hand one moves to the middle. */
    const MenuEntry *header = entry(Extras_header);
    const int32_t mid = header->x + header->dwWidth / 2;
    for (int32_t i = Extras_editTeams; i <= Extras_manual; i++) {
        MenuEntry *e = entry(i);
        e->x = (uint16_t)(mid - e->dwWidth / 2);
        e->leftEntry = -1;
    }
    entry(Extras_manual)->downEntry = Extras_exit;
    entry(Extras_exit)->upEntry = Extras_manual;
    SetCurrentEntry(Extras_editTeams);
}

static void editTeams(void)   { choose(EditCustomTeamMenu); }
static void editTactics(void) { choose(EditTacticsMenu); }
static void diskFiling(void)  { choose(SaveDiskFilingMenu); }

/* INSTRUCTION MANUAL: the one SWOS 2020 opens (manualExtras_OnSelect), in the browser. */
static void manual(void)
{
    Platform_OpenUrl("https://www.sensiblesoccer.de/swos2020/swos_96_97_manual.pdf");
}

/* ====================================================================================== */
static void init(void)
{
    static const ModHandler handlers[] = {
        { "mainInit", mainInit },
        { "compete", compete }, { "practice", practice }, { "replays", replays },
        { "options", options }, { "editing", editing }, { "extras", extras },
        { "continueContest", continueContest }, { "saveContest", saveContest },
        { "loadContest", loadContest }, { "quit", quit }, { "credits", credits },

        { "competitionInit", competitionInit },
        { "online", online }, { "diy", diy }, { "preset", preset }, { "career", career },
        { "speedruns", speedruns }, { "challenges", challenges }, { "season", season },

        { "practiceInit", practiceInit },
        { "friendly", friendly }, { "modes", modes },

        { "extrasInit", extrasInit },
        { "editTeams", editTeams }, { "editTactics", editTactics }, { "manual", manual },
        { "diskFiling", diskFiling },
    };
    ModApi_RegisterHandlers(SLUG, handlers, (int32_t)(sizeof handlers / sizeof handlers[0]));
}

const ModDef ModDef_menus2020 = {
    .slug = SLUG,
    .init = init,
};

#endif /* SWOS_SDL_BUILD */
