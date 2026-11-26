/*
 * mod_menus2020.c -- SWOS United's menus2020 mod: SWOS 2020's main menu and the screens its
 * buttons open (docs/SWOS2020_MODS.md, "menus2020"). SDL builds; United only (src/fork_mods.inc).
 *
 * The menus are data, made from SWOS 2020's .mnu files by scripts/make_menus2020.py into
 * bin/mods/menus2020/data_json/menus/: swosMainMenu.json replaces the game's main menu through
 * the mod overlay while the mod is on, the rest are its own screens (menus2020/<menu>). Every
 * handler they name is one of these, "menus2020.<name>" (ModApi_RegisterHandlers).
 *
 * SWOS 2020's main menu as it is built (MULTIPLAYER_MENU): main.mnu's six buttons, relabelled.
 * What they do here, by what they say:
 *   MULTIPLAYER  the game's MULTIPLAYER (SWOS 2020's online flow, docs/MENUS.md §10)
 *   COMPETE    COMPETITIONS: ONLINE (the same), DIY, PRESET, CLASSIC CAREER, and SPEEDRUNS /
 *              CHALLENGES when OpenSWOS's speedrun / challenges mods are on
 *   PRACTICE   FRIENDLY MATCH; TEAM TRAINING, PENALTY SHOOTOUT, DEMO GAME (OpenSWOS's practice
 *              mod, when on); MODES -- the mods' entries (MODPOINT_MODES) -- when another has one
 *   OPTIONS    the game's OPTIONS
 *   EXTRAS     TIPS AND REVIEWS, STREAMS AND VIDEOS (the ticker's news, listed), EDIT CUSTOM
 *              TEAMS, EDIT TACTICS, INSTRUCTION MANUAL (LATEST NEWS hidden: no source yet)
 *   REPLAYS    the game's HIGHLIGHTS
 *   CONTINUE / SAVE / LOAD, QUIT, CREDITS; the news ticker under them
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
#include <stdlib.h>
#include <string.h>
#include <strings.h>          /* strncasecmp */

#include "dos_graphics.h"       /* DosGfx_DecodePlanarSprite */
#include "dos_mode.h"           /* _gDosGraphicsDir */
#include "edit_tactics.h"       /* FillSkinColorConversionTable */
#include "file_io.h"            /* LoadFileEx */
#include "swos_io.h"            /* swos_data_path: is the run-in's file there */
#include "math68k.h"            /* CalculateDeltaXAndY */
#include "gfx.h"
#include "easing.h"            /* the run-in's ease */
#include "menu_load_json.h"     /* MenuJson_VerifyDynamic (SWOS_MENUS2020_TEST) */
#include "menu_system.h"
#include "mjson.h"
#include "menus.h"
#include "platform_sdl.h"       /* Platform_OpenUrl, Platform_RequestQuit */
#include "united_ticker.h"       /* UnitedTicker_News */
#include "menu_load_json.h"      /* MenuJson_RegisterSymbols: REPLAYS' handlers, by SWOS 2020's names */
#include "menu_symbols.h"
#include "swos2020_menus.h"

#define SLUG "menus2020"

extern int16_t DoContinueAbortMenu(uint8_t *header, uint8_t *changing, uint8_t *contText,
                                   uint8_t *abortTextStr, uint16_t defaultEntry);
extern void MultiplayerMenuSelected(void);
extern void CreditsMenuSelected(void);
extern void ModesMenuSelected(void);
extern void CareerSeasons_Classic(void);
extern void CareerSeasons_World(void);

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
    Main_multi, Main_compete, Main_practice,        /* (main.mnu's competition, practice, replays) */
    Main_options, Main_extras, Main_replays,        /* (its options, editing, extras) */
    Main_quit, Main_ticker, Main_liveCast, Main_logo,
    /* 14 and 16, the game's own MULTIPLAYER and MODES places: SWOS United's two careers
     * (scripts/make_menus2020.py CAREER_BUTTONS; src/career_seasons.c) */
    Main_continue = 11, Main_save, Main_load, Main_classicCareer, Main_credits, Main_worldCareer,
};

static uint16_t s_loadX, s_loadY;       /* LOAD's place under CONTINUE, as unpacked */
static int32_t  s_loadKnown;

/* After CommonMenuExit (run by onInit and by every game handler): it shows CONTINUE / SAVE
 * only with a competition on, moving LOAD up to CONTINUE's row without one, and shows MODES
 * when a mod has an entry. Here: MULTIPLAYER and MODES stay hidden (COMPETE and PRACTICE
 * have them), LOAD is centred on CONTINUE's row when it is alone, and the arrows go where
 * the buttons are. */
static int32_t s_liveShown = -2;        /* the TV's picture: 1 live, 0 idle, -2 as unpacked */
static uint16_t s_tickerX, s_tickerY, s_tickerW;    /* the ticker's place, as unpacked */
static int32_t  s_tickerKnown;

static int32_t s_runState;              /* the run-in (below): 0 off, 1 waiting, 2 running, 3 settling, 4 done */

/* WORLD CAREER's text (Playa, 2026-11-26): the season of the World 2026-27 package installed -- its
 * WORLD.ini's name ("World 2026-27": WORLD 26/27) -- or WORLD CAREER when there is none, or no
 * season in its name. Read once: OpenSWOS Setup installs it with the game shut. */
static char s_worldName[16];

/* "2026-27", "2026/27", "26/27", "2026-2027" in `s`: 1 and the two years' last two digits. */
static int32_t season_in(const char *s, int32_t *y1, int32_t *y2)
{
    for (const char *p = s; *p; p++) {
        int32_t n1 = 0, n2 = 0, l1 = 0, l2 = 0;
        const char *q = p;
        if (p > s && p[-1] >= '0' && p[-1] <= '9')
            continue;
        while (*q >= '0' && *q <= '9' && l1 < 5) { n1 = n1 * 10 + (*q++ - '0'); l1++; }
        if ((l1 != 2 && l1 != 4) || (*q != '-' && *q != '/'))
            continue;
        q++;
        while (*q >= '0' && *q <= '9' && l2 < 5) { n2 = n2 * 10 + (*q++ - '0'); l2++; }
        if ((l2 != 2 && l2 != 4) || n2 % 100 != (n1 + 1) % 100)
            continue;
        *y1 = n1 % 100;
        *y2 = n2 % 100;
        return 1;
    }
    return 0;
}

static void world_name(void)
{
    char path[1024], line[256];
    int32_t y1 = 0, y2 = 0, found = 0;
    FILE *f = fopen(swos_data_path("data_json/worlds/2627/WORLD.ini", path, (uint32_t)sizeof path), "rb");
    while (f && !found && fgets(line, (int)sizeof line, f)) {
        const char *p = line + strspn(line, " \t");
        if (strncasecmp(p, "name", 4) == 0 && strchr(p, '='))
            found = season_in(strchr(p, '=') + 1, &y1, &y2);
    }
    if (f)
        fclose(f);
    if (found)
        snprintf(s_worldName, sizeof s_worldName, "WORLD %02d/%02d", (int)y1, (int)y2);
    else
        snprintf(s_worldName, sizeof s_worldName, "WORLD CAREER");
}

/* ... the globe right of it, as the right column's icons, the two centred together in the box:
 * spaces after the text as wide as the gap and the icon (the game centres the text with them), the
 * icon at their end. */
enum { kCareerIconW = 16, kCareerIconGap = 4, kCareerIconY = 4 };
static void world_button(void)
{
    static char text[40];       /* (the menu keeps the pointer) */
    if (!s_worldName[0])
        world_name();
    const int32_t w = ModApi_TextWidth(s_worldName, 0), sp = ModApi_TextWidth(" ", 0);
    const int32_t n = sp > 0 ? (kCareerIconGap + kCareerIconW + sp / 2) / sp : 5;
    const int32_t box = entry(Main_worldCareer)->dwWidth;
    const int32_t start = (box - w - n * sp) / 2;
    snprintf(text, sizeof text, "%s%*s", s_worldName, (int)n, "");
    ModApi_MenuText(Main_worldCareer, text);
    ModApi_SetMenuImageAt(Main_worldCareer, start + w + n * sp - kCareerIconW, kCareerIconY);
    if (getenv("SWOS_WORLD_BUTTON_DEBUG"))
        fprintf(stderr, "[menus2020] world button \"%s\": text %d + %d spaces of %d, icon at %d\n",
                s_worldName, (int)w, (int)n, (int)sp, (int)(start + w + n * sp - kCareerIconW));
}

static void main_refresh(void)
{
    MenuEntry *cont = entry(Main_continue), *load = entry(Main_load);
    const int32_t contest = !cont->isInvisible;

    show(Main_classicCareer, 1);       /* (CommonMenuExit hides 14 and shows 16 for MODES) */
    show(Main_worldCareer, 1);
    world_button();
    nav(Main_classicCareer, -1, Main_worldCareer, Main_practice, contest ? Main_continue : Main_load);
    nav(Main_worldCareer, Main_classicCareer, -1, Main_replays, contest ? Main_continue : Main_load);
    if (s_runState >= 1 && s_runState <= 3)     /* (unpacked again mid-run: no picture until they settle) */
        ModApi_SetMenuImage(Main_credits, NULL);
    s_liveShown = -2;                   /* (unpacked again: the JSON's idle picture) */
    {   /* The ticker under SAVE and LOAD, as SWOS 2020's -- or across the top (where it was) on a
         * screen with no rows under the menu; back under them when there are again. */
        MenuEntry *t = entry(Main_ticker);
        int32_t sw = 0, sh = 0;
        ModApi_ScreenSize(&sw, &sh);
        if (!s_tickerKnown) {
            s_tickerX = t->x;
            s_tickerY = t->y;
            s_tickerW = t->dwWidth;
            s_tickerKnown = 1;
        }
        if (s_tickerY + t->dwHeight > sh) {
            t->x = 50;
            t->y = 6;
            t->dwWidth = 262;
        } else {
            t->x = s_tickerX;
            t->y = s_tickerY;
            t->dwWidth = s_tickerW;
        }
    }
#ifdef SWOS_NETPLAY
    nav(Main_multi, -1, Main_options, Main_liveCast, Main_compete);
#else
    /* No online play in this build: MULTIPLAYER and LIVE CAST go, COMPETE is the top. */
    show(Main_multi, 0);
    show(Main_liveCast, 0);
    nav(Main_compete, -1, Main_extras, -1, Main_practice);
    nav(Main_options, Main_compete, -1, -1, Main_extras);
    if (ModApi_MenuCurrentEntry() == Main_multi)
        SetCurrentEntry(Main_compete);
#endif

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
        nav(Main_practice, -1, Main_replays, Main_compete, Main_classicCareer);
        nav(Main_replays, Main_practice, -1, Main_extras, Main_worldCareer);
        nav(Main_continue, -1, -1, Main_classicCareer, Main_save);
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
        nav(Main_practice, -1, Main_replays, Main_compete, Main_classicCareer);
        nav(Main_replays, Main_practice, -1, Main_extras, Main_worldCareer);
        nav(Main_load, Main_quit, Main_credits, Main_classicCareer, -1);
        nav(Main_quit, -1, Main_load, Main_classicCareer, -1);
        nav(Main_credits, Main_load, -1, Main_worldCareer, -1);
    }
}

/* ---- SWOS 2020's PixelAnimation: three little players -- red, green and blue, their faces dark,
 * ginger and fair -- run in from just off the top left each time the main menu opens and stand at
 * the bottom right, where the CREDITS entry's picture -- the SWOS United logo, the same three
 * standing (swos_united_logo_24x14_1x1_Map, made on the player's machine by
 * scripts/make_menus2020_icons.py) -- then takes their place, and stays. Its sprites: the PC game's
 * TEAM2.DAT, its first 24 (the running and standing frames of the Amiga's 227..250, eight ways),
 * coloured as its colorAllSprites does. Without the logo they stay there standing (they are the
 * button); without the PC game's data the logo at once (or an empty entry, still selectable).
 *
 * Each runs a cubic Bezier from the shared start to its place: two control points chosen at random
 * each time the menu opens (a sweep through the screen, then coming down onto the button), the
 * path's parameter eased (include/easing.h, Penner's curves in 16.16) over the run's time; they set
 * off a little apart, so as not to run as one. At the end the parameter is exactly 1 and the curve
 * exactly at its last point: every player stands on its place, never short of it nor past it. The
 * places come from the picture's (run_places): each sprite stands where its player is in the logo
 * (6 px apart, as SWOS 2020's kEndPositions), so the swap does not jump. The legs step with the
 * distance covered (slower as the ease settles), the way it faces is the curve's direction. Drawn
 * into the 8bpp menu picture (ModApi_Frame) from menuDrawn, in the menu's coordinates (the wide
 * screen's margin added): the main menu is redrawn whole each frame (its dos_redraw), so nothing
 * under them needs keeping. */
enum { kRunPlayers = 3, kRunImages = 24 };
enum { kRunDelayMs = 500, kRunSettleMs = 540, kRunStridePx = 7 };   /* (kPlayersSettledDelay: 38 of 70) */
/* The logo's place in the CREDITS entry (make_menus2020.py CREDITS_PICTURE_AT: keep the two the
 * same), and its players 6 px apart, each drawn as the sprite's top left (SWOS 2020's DrawSprite). */
enum { kCreditsPicX = 4, kCreditsPicY = 3, kCreditsPicStep = 6 };
enum { kRunStand = 3 };                             /* (kRunFrames[4][1]: standing, facing down) */
static const int16_t kRunFrames[8][4] = {          /* (_playerRunning*Team1, up first, clockwise) */
    { 2, 0, 1, 0 }, { 23, 22, 21, 22 }, { 8, 6, 7, 6 }, { 17, 16, 15, 16 },
    { 5, 3, 4, 3 }, { 14, 13, 12, 13 }, { 11, 9, 10, 9 }, { 20, 19, 18, 19 },
};
static const int16_t  kRunStartXY[2] = { -16, -20 };    /* just off the screen's top left */
static const uint16_t kRunSetOffMs[kRunPlayers] = { 0, 150, 300 };
static const uint16_t kRunDurationMs[kRunPlayers] = { 7000, 7600, 6600 };
static const uint8_t  kRunKit[kRunPlayers] = { 10, 14, 11 };
static const uint8_t  kRunFace[kRunPlayers] = { 2, 1, 0 };
static struct { int16_t w, pixW, h, cx, cy; uint8_t *pix; } s_runImg[kRunImages];
static int32_t  s_runLoaded;                    /* 0 not tried, 1 ready, -1 no data */
static struct {
    int32_t  p[4][2];                           /* the curve's four points, whole pixels */
    uint32_t setOffMs, durationMs;
    int32_t  lastX, lastY, travelled;           /* (the legs: pixels since the last step) */
    int16_t  image;
    uint8_t  dir, step, arrived;
    uint16_t colours[16];                       /* sprite colour -> the menu palette's */
} s_run[kRunPlayers];
static uint32_t s_runStartMs, s_runSeed;
static int32_t  s_runPicture = -1;              /* the logo is there: 1, 0 not, -1 not looked */

static int32_t run_picture(void)
{
    if (s_runPicture < 0) {
        char path[1200];
        const char *dir = ModApi_ModDir(SLUG);
        FILE *f;
        snprintf(path, sizeof path, "%s/grafs_2020/menus2020/credits.bmp", dir ? dir : ".");
        f = fopen(path, "rb");
        s_runPicture = f != NULL;
        if (f)
            fclose(f);
    }
    return s_runPicture;
}

static void run_load(void)
{
    static uint8_t file[64 * 1024];
    char path[300];
    uint32_t size = 0, at = 0;
    s_runLoaded = -1;
    snprintf(path, sizeof path, "%s/team2.dat", _gDosGraphicsDir);
    {   /* (the PC game's sprites: game data made without the PC game -- the Amiga's, SWOS 2020's --
         *  has none. LoadFileEx retries a missing file for ever, the original's "insert the disk",
         *  so the main menu never came: testers' "hangs fading to the main menu", 2026-11-25. No
         *  file, no run-in.) */
        char real[1200];
        FILE *f = fopen(swos_data_path(path, real, (uint32_t)sizeof real), "rb");
        if (!f) {
            fprintf(stderr, "[menus2020] no %s (game data without the PC game's): no run-in\n", path);
            return;
        }
        fclose(f);
    }
    if (LoadFileEx((uint8_t *)path, file, &size) != 0 || size == 0 || size > sizeof file)
        return;
    for (int32_t i = 0; i < kRunImages; i++) {
        if (at + 24 > size)
            return;
        const uint8_t *h = file + at;
        const int16_t w = (int16_t)(h[10] | h[11] << 8);
        const int16_t n = (int16_t)(h[12] | h[13] << 8);
        const int16_t q = (int16_t)(h[14] | h[15] << 8);
        at += 24;
        if (w <= 0 || n <= 0 || q <= 0 || at + (uint32_t)(n * q * 8) > size)
            return;
        s_runImg[i].pix = (uint8_t *)malloc((size_t)n * q * 16);
        if (!s_runImg[i].pix)
            return;
        DosGfx_DecodePlanarSprite(file + at, n, q, s_runImg[i].pix);
        at += (uint32_t)(n * q * 8);
        s_runImg[i].w = (int16_t)(q * 16);          /* (the rows' stride: pixWidth fits in it) */
        s_runImg[i].pixW = w;
        s_runImg[i].h = n;
        s_runImg[i].cx = (int16_t)(h[16] | h[17] << 8);
        s_runImg[i].cy = (int16_t)(h[18] | h[19] << 8);
    }
    s_runLoaded = 1;
}

/* The players' sprites are there (loaded the first time asked). */
static int32_t run_ready(void)
{
    if (!s_runLoaded)
        run_load();
    return s_runLoaded == 1;
}

static int32_t run_rand(int32_t n)              /* 0..n-1 */
{
    s_runSeed = s_runSeed * 1103515245u + 12345u;
    return n > 0 ? (int32_t)((s_runSeed >> 16) % (uint32_t)n) : 0;
}

/* Each player's place -- its hot spot, in the menu's coordinates: where its player stands in the
 * logo, the picture's top left + 6 px a player, then the sprite's hot spot. */
static void run_places(int32_t place[kRunPlayers][2])
{
    int32_t bx = 0, by = 0;
    ModApi_MenuEntryRect(Main_credits, &bx, &by, NULL, NULL);
    for (int32_t i = 0; i < kRunPlayers; i++) {
        place[i][0] = bx + kCreditsPicX + i * kCreditsPicStep + s_runImg[kRunStand].cx;
        place[i][1] = by + kCreditsPicY + s_runImg[kRunStand].cy;
    }
}

/* The curve at u (16.16, 0..EASE_ONE), whole pixels. At EASE_ONE the weights are 0, 0, 0, 1: the
 * last point exactly. */
static void run_bezier(int32_t pl, EaseFix u, int32_t *x, int32_t *y, int32_t *dx, int32_t *dy)
{
    const int64_t v = EASE_ONE - u;
    const int64_t w0 = v * v / EASE_ONE * v / EASE_ONE;
    const int64_t w1 = 3 * v * v / EASE_ONE * u / EASE_ONE;
    const int64_t w2 = 3 * v * u / EASE_ONE * u / EASE_ONE;
    const int64_t w3 = (int64_t)u * u / EASE_ONE * u / EASE_ONE;
    const int32_t (*P)[2] = s_run[pl].p;
    int32_t at[2], dir[2];
    for (int32_t k = 0; k < 2; k++) {
        const int64_t c = w0 * P[0][k] + w1 * P[1][k] + w2 * P[2][k] + w3 * P[3][k];
        /* the direction: B'(u) / 3 = v^2 (P1-P0) + 2uv (P2-P1) + u^2 (P3-P2) */
        const int64_t d = v * v / EASE_ONE * (P[1][k] - P[0][k]) + 2 * u * v / EASE_ONE * (P[2][k] - P[1][k]) +
                          (int64_t)u * u / EASE_ONE * (P[3][k] - P[2][k]);
        at[k] = (int32_t)((c + (c >= 0 ? EASE_ONE / 2 : -EASE_ONE / 2)) / EASE_ONE);
        dir[k] = (int32_t)d;
    }
    if (u >= EASE_ONE) {
        at[0] = P[3][0];
        at[1] = P[3][1];
    }
    *x = at[0];
    *y = at[1];
    *dx = dir[0];
    *dy = dir[1];
}

/* Eight ways from up, clockwise (y grows down), from a direction: 22.5 degrees each side. */
static uint8_t run_way(int32_t dx, int32_t dy)
{
    const int64_t ax = dx < 0 ? -(int64_t)dx : dx, ay = dy < 0 ? -(int64_t)dy : dy;
    if (ay * 2 > ax * 5)                         /* (tan 67.5 ~ 2.41: nearly vertical) */
        return dy < 0 ? 0 : 4;
    if (ax * 2 > ay * 5)                         /* nearly horizontal */
        return dx > 0 ? 2 : 6;
    return dx > 0 ? (dy < 0 ? 1 : 3) : (dy < 0 ? 7 : 5);
}

static void run_begin(void)
{
    int32_t place[kRunPlayers][2];
    if (!run_ready()) {
        s_runState = 0;                         /* (the logo at once, as the JSON has it) */
        return;
    }
    run_places(place);
    s_runSeed = ModApi_Ticks() * 2654435761u + 1u;
    const int32_t sx = kRunStartXY[0] - ModApi_MenuMargin(), sy = kRunStartXY[1];
    for (int32_t i = 0; i < kRunPlayers; i++) {
        const int32_t ex = place[i][0], ey = place[i][1];
        int32_t (*P)[2] = s_run[i].p;
        P[0][0] = sx;
        P[0][1] = sy;
        /* first a sweep out into the screen, then down onto the button from above it -- never
         * right of it, so they do not run past it */
        P[1][0] = 40 + run_rand(ex - 80 > 1 ? ex - 80 : 1);
        P[1][1] = 30 + run_rand(ey - 100 > 1 ? ey - 100 : 1);
        P[2][0] = ex - 70 + run_rand(71);
        P[2][1] = ey - 100 + run_rand(61);
        P[3][0] = ex;
        P[3][1] = ey;
        s_run[i].setOffMs = kRunSetOffMs[i];
        s_run[i].durationMs = kRunDurationMs[i] + (uint32_t)run_rand(400);
        s_run[i].lastX = sx;
        s_run[i].lastY = sy;
        s_run[i].travelled = 0;
        s_run[i].dir = 3;
        s_run[i].step = 0;
        s_run[i].arrived = 0;
        s_run[i].image = kRunFrames[3][0];
        uint16_t table[16] = { 0, 1, 2, 3, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0 };   /* (setSpriteColors) */
        table[10] = kRunKit[i];
        table[11] = kRunKit[i];
        table[15] = kRunKit[i];
        table[14] = 2;
        FillSkinColorConversionTable(kRunFace[i], table);
        memcpy(s_run[i].colours, table, sizeof table);
    }
    s_runState = 1;
    s_runStartMs = ModApi_Ticks();
    ModApi_SetMenuImage(Main_credits, NULL);    /* (the logo when they are there) */
}

/* After they settle: the logo in their place, for good; without it they stay, standing. */
static void run_settle(void)
{
    if (s_runState != 3 || ModApi_Ticks() - s_runStartMs < kRunSettleMs)
        return;
    s_runState = 4;
    if (run_picture()) {
        ModApi_SetMenuImage(Main_credits, "grafs_2020/menus2020/credits.bmp");
        ModApi_MenuRedraw(Main_credits);
    }
}

/* Where each is now, from the time since they set off. */
static void run_update(void)
{
    const uint32_t now = ModApi_Ticks(), since = now - s_runStartMs;
    if (s_runState == 1) {
        if (since < kRunDelayMs)
            return;
        s_runState = 2;
        s_runStartMs += kRunDelayMs;
        return;
    }
    if (s_runState != 2) {
        run_settle();
        return;
    }
    int32_t all = 1;
    for (int32_t i = 0; i < kRunPlayers; i++) {
        if (s_run[i].arrived)
            continue;
        all = 0;
        if (since < s_run[i].setOffMs)
            continue;                                /* (not off yet: off the screen) */
        const uint32_t ran = since - s_run[i].setOffMs;
        const EaseFix t = ran >= s_run[i].durationMs ? EASE_ONE
                        : (EaseFix)((int64_t)ran * EASE_ONE / s_run[i].durationMs);
        const EaseFix u = t >= EASE_ONE ? EASE_ONE : Ease(EASE_IN_OUT_SINE, t);
        int32_t x = 0, y = 0, dx = 0, dy = 0;
        run_bezier(i, u, &x, &y, &dx, &dy);
        const int32_t mx = x - s_run[i].lastX < 0 ? s_run[i].lastX - x : x - s_run[i].lastX;
        const int32_t my = y - s_run[i].lastY < 0 ? s_run[i].lastY - y : y - s_run[i].lastY;
        s_run[i].travelled += mx > my ? mx : my;
        s_run[i].lastX = x;
        s_run[i].lastY = y;
        if (dx || dy)
            s_run[i].dir = run_way(dx, dy);
        while (s_run[i].travelled >= kRunStridePx) {
            s_run[i].travelled -= kRunStridePx;
            s_run[i].step = (uint8_t)((s_run[i].step + 1) & 3);
        }
        if (t >= EASE_ONE) {                         /* there: on its place exactly */
            s_run[i].arrived = 1;
            s_run[i].image = kRunStand;
            if (getenv("SWOS_RUNIN_TEST")) {         /* (the run-in's check: every one on its place) */
                int32_t place[kRunPlayers][2], bx = 0, by = 0;
                const int32_t m = ModApi_MenuMargin();
                run_places(place);
                ModApi_MenuEntryRect(Main_credits, &bx, &by, NULL, NULL);
                fprintf(stderr, "[runin-test] player %d at %d,%d, its place %d,%d: %s; on the screen its "
                        "sprite %d,%d %dx%d, its player in the logo %d,%d (the logo %d,%d 24x14)\n",
                        (int)i, (int)x, (int)y, (int)place[i][0], (int)place[i][1],
                        x == place[i][0] && y == place[i][1] ? "PASS" : "FAIL",
                        (int)(m + x - s_runImg[kRunStand].cx), (int)(y - s_runImg[kRunStand].cy),
                        (int)s_runImg[kRunStand].pixW, (int)s_runImg[kRunStand].h,
                        (int)(m + bx + kCreditsPicX + i * kCreditsPicStep), (int)(by + kCreditsPicY),
                        (int)(m + bx + kCreditsPicX), (int)(by + kCreditsPicY));
            }
            continue;
        }
        s_run[i].image = kRunFrames[s_run[i].dir][s_run[i].step];
    }
    if (all) {
        s_runState = 3;                              /* (playersSettled: then the logo) */
        s_runStartMs = now;
    }
}

/* One player's picture into the 8bpp frame at (x, y) -- its hot spot -- 0 see-through. */
static void run_blit(uint8_t *frame, int32_t stride, int32_t fh, int32_t pl, int32_t x, int32_t y)
{
    const int32_t img = s_run[pl].image;
    if (img < 0 || img >= kRunImages || !s_runImg[img].pix)
        return;
    const int32_t w = s_runImg[img].w, h = s_runImg[img].h;
    const int32_t x0 = x - s_runImg[img].cx, y0 = y - s_runImg[img].cy;
    for (int32_t r = 0; r < h; r++) {
        const int32_t fy = y0 + r;
        if (fy < 0 || fy >= fh)
            continue;
        const uint8_t *src = s_runImg[img].pix + r * w;
        uint8_t *dst = frame + fy * stride;
        for (int32_t c = 0; c < w; c++) {
            const int32_t fx = x0 + c;
            const uint8_t v = src[c];
            if (!v || fx < 0 || fx >= stride)
                continue;
            const uint8_t col = v < 16 ? (uint8_t)s_run[pl].colours[v] : v;
            if (col)
                dst[fx] = col;
        }
    }
}

/* Each main-menu frame (menuDrawn): move them, and draw them over the menu (drawPlayers: the
 * middle one on top) -- until the logo takes their place; without it, standing there for good. */
static void run_menu_frame(void)
{
    static const int32_t kOrder[kRunPlayers] = { 0, 2, 1 };
    int32_t sw = 0, sh = 0, stride = 0;
    if (!s_runState || !ModApi_IsMainMenu())
        return;
    run_update();
    if (s_runState < 2 || (s_runState == 4 && run_picture()))
        return;
    uint8_t *frame = ModApi_Frame(&stride);
    if (!frame || stride <= 0)
        return;
    ModApi_ScreenSize(&sw, &sh);
    const int32_t ox = ModApi_MenuMargin();
    for (int32_t k = 0; k < kRunPlayers; k++) {
        const int32_t i = kOrder[k];
        run_blit(frame, stride, sh, i, ox + s_run[i].lastX, s_run[i].lastY);
    }
}

/* SWOS_MENUS2020_TEST=1: each of the mod's menus loaded and unpacked through the real unpacker
 * and checked against its JSON (MenuJson_VerifyDynamic: the entry count, places, links, texts),
 * once the main menu is up -- the mod on, its overlay in place (--menu-selftest runs before the
 * mods are) -- then the game exits, 0 when all pass. */
static int32_t json_entries(const char *name)
{
    char path[1200], q[48], kind[24];
    uint32_t size = 0;
    int32_t n = 0;
    const char *dir = ModApi_ModDir(SLUG);
    snprintf(path, sizeof path, "%s/data_json/menus/%s.json", dir ? dir : ".", name);
    char *j = (char *)ModApi_ReadFile(path, &size);
    if (!j)
        return -1;
    for (int32_t i = 0; i < 4096; i++) {
        snprintf(q, sizeof q, "$.items[%d].kind", (int)i);
        const char *tok = NULL;
        int tl = 0;
        if (mjson_find(j, (int)size, q, &tok, &tl) == MJSON_TOK_INVALID) {
            snprintf(q, sizeof q, "$.items[%d]", (int)i);
            if (mjson_find(j, (int)size, q, &tok, &tl) == MJSON_TOK_INVALID)
                break;
            continue;
        }
        if (mjson_get_string(j, (int)size, q, kind, (int)sizeof kind) > 0 && !strcmp(kind, "entry"))
            n++;
    }
    free(j);
    return n;
}

static void menus_selftest(void)
{
    static const char *const kMenus[] = {
        "swosMainMenu", SLUG "/competitionMenu", SLUG "/practiceMenu", SLUG "/editingMenu",
        SLUG "/extrasMenu", SLUG "/quitMenu", SLUG "/tickerMenu", SLUG "/liveMenu",
    };
    int32_t failed = 0;
    for (uint32_t i = 0; i < sizeof kMenus / sizeof kMenus[0]; i++) {
        const int32_t want = json_entries(kMenus[i]);
        const int bad = want < 0 ? -1 : MenuJson_VerifyDynamic(kMenus[i], (int)want);
        fprintf(stderr, "[menus2020-test] %-28s %3d entries: %s\n", kMenus[i], (int)want,
                bad == 0 ? "PASS" : bad < 0 ? "FAIL (did not load)" : "FAIL (mismatch, see above)");
        failed += bad != 0;
    }
    fprintf(stderr, "[menus2020-test] %d menus, %d failed\n", (int)(sizeof kMenus / sizeof kMenus[0]), (int)failed);
    exit(failed ? 1 : 0);
}

/* SWOS_KIT_SHEET=<file>: at the main menu, the little player (the stadium screen's, facing forward, a
 * plain shirt) in each kit colour the menus step through, in their order -- shirt, shorts and socks
 * that colour, the menu palette's entries (TeamColors_MenuEntry), in each of the three skin and hair
 * tones (FillSkinColorConversionTable 0-2) -- written as RGBA for scripts/make_kit_sheet.py: "KITS",
 * u32 count, width, height, tones, then per colour its byte and, a tone each, width x height RGBA
 * (alpha 0: not the player). Exits. */
static void kit_sheet_dump(const char *path)
{
    extern EditTeamsSpriteCopy _stadiumMenuLittlePlayerSpriteCopy;
    extern uint16_t _littlePlayersSpriteTable[];
    extern uint16_t _menuPalette[];
    extern void *_spriteGraphicsPtr;
    extern void TeamColors_MenuVariants(void);
    extern uint16_t TeamColors_MenuEntry(uint8_t colour);
    extern int32_t KitColour_StepCount(void);
    extern uint8_t KitColour_FromStepIndex(int32_t index);
    SpriteGraphics **t = (SpriteGraphics **)_spriteGraphicsPtr;
    const SpriteGraphics *sp = t ? t[_littlePlayersSpriteTable[0]] : NULL;
    FILE *f = fopen(path, "wb");
    int32_t ok = 0;
    if (sp && sp->sprData && f) {
        uint16_t ct[16];
        const int32_t w = sp->pixWidth, h = sp->nlines, stride = sp->wquads * 16, n = KitColour_StepCount();
        const uint32_t head[4] = { (uint32_t)n, (uint32_t)w, (uint32_t)h, 3 };
        TeamColors_MenuVariants();
        fwrite("KITS", 1, 4, f);
        fwrite(head, 4, 4, f);
        for (int32_t i = 0; i < n; i++) {
            const uint8_t colour = KitColour_FromStepIndex(i);
            const uint16_t e = TeamColors_MenuEntry(colour);
            fwrite(&colour, 1, 1, f);
            for (int32_t tone = 0; tone < 3; tone++) {
            memcpy(ct, _stadiumMenuLittlePlayerSpriteCopy.colTable, sizeof ct);
            FillSkinColorConversionTable((uint16_t)tone, ct);
            ct[10] = ct[11] = ct[14] = ct[15] = e;
            for (int32_t y = 0; y < h; y++)
                for (int32_t x = 0; x < w; x++) {
                    const uint8_t p = sp->sprData[y * stride + x];
                    const uint16_t rgb = p ? _menuPalette[ct[p & 15] & 0xFF] : 0;
                    const uint8_t px[4] = { (uint8_t)((rgb >> 8 & 15) * 17), (uint8_t)((rgb >> 4 & 15) * 17),
                                            (uint8_t)((rgb & 15) * 17), (uint8_t)(p ? 255 : 0) };
                    fwrite(px, 1, 4, f);
                }
            }
        }
        ok = 1;
        printf("KIT SHEET: %d colours, the player %d x %d -> %s\n", (int)n, (int)w, (int)h, path);
    }
    if (f)
        fclose(f);
    fflush(stdout);
    exit(ok ? 0 : 1);
}

/* SWOS_SHIRT_DUMP=<file>: the EDIT TEAM / UNIFORM screens' shirt (sprite 190, its pixels in eight
 * region colours: kit_art.c) for scripts/make_shirt_sheet.py -- "SHRT", u32 width, height, its pixel
 * indices, the menu palette (256 x 0x0RGB, u16), the shirt types' regions (_shirtsIndexTable 8 x u16,
 * _shirtTypesTable 32 x u16) and the 11 kit colours' entries (_kTeamColorsTable). Exits. */
static void shirt_dump(const char *path)
{
    extern uint16_t _menuPalette[256], _shirtTypesTable[32], _shirtsIndexTable[8], _kTeamColorsTable[];
    extern void *_spriteGraphicsPtr;
    SpriteGraphics **t = (SpriteGraphics **)_spriteGraphicsPtr;
    const SpriteGraphics *sp = t ? t[190] : NULL;
    FILE *f = fopen(path, "wb");
    int32_t ok = 0;
    if (sp && sp->sprData && f) {
        const uint32_t head[2] = { (uint32_t)sp->pixWidth, (uint32_t)sp->nlines };
        fwrite("SHRT", 1, 4, f);
        fwrite(head, 4, 2, f);
        for (int32_t y = 0; y < sp->nlines; y++)
            fwrite(sp->sprData + y * sp->wquads * 16, 1, (size_t)sp->pixWidth, f);
        fwrite(_menuPalette, 2, 256, f);
        fwrite(_shirtsIndexTable, 2, 8, f);
        fwrite(_shirtTypesTable, 2, 32, f);
        fwrite(_kTeamColorsTable, 2, 11, f);
        ok = 1;
        printf("SHIRT DUMP: %d x %d -> %s\n", (int)sp->pixWidth, (int)sp->nlines, path);
    }
    if (f)
        fclose(f);
    fflush(stdout);
    exit(ok ? 0 : 1);
}

/* The logo as the player chose ([mod.menus2020] logo): its pixels, or its HD copy drawn smooth at
 * the window's resolution (scripts/make_united_logo.py, menu_extras.c) -- set as the menu opens and
 * as it is come back to (from OPTIONS -> MODS, where it is chosen). */
static void logo_as_chosen(void)
{
    ModApi_SetMenuImageHD(Main_logo, strcmp(ModApi_Setting(SLUG, "logo", "pixel"), "hd") == 0
                                     ? "grafs_united/logo_hd.png" : NULL);
}

static void mainInit(void)
{
    logo_as_chosen();
    if (getenv("SWOS_SHIRT_DUMP"))
        shirt_dump(getenv("SWOS_SHIRT_DUMP"));
    if (getenv("SWOS_PLAYER_DUMP")) {   /* (the PC graphics' match players: color_sprites.c) */
        extern int ColorSprites_DumpDosPlayers(const char *path);
        const int rc = ColorSprites_DumpDosPlayers(getenv("SWOS_PLAYER_DUMP"));
        printf("PLAYER DUMP: %s\n", rc == 0 ? "written" : "FAILED");
        fflush(stdout);
        exit(rc == 0 ? 0 : 1);
    }
    if (getenv("SWOS_KIT_SHEET"))
        kit_sheet_dump(getenv("SWOS_KIT_SHEET"));
    extern void SWOSMainMenuInit(void);
    if (getenv("SWOS_MENUS2020_TEST"))
        menus_selftest();
    SWOSMainMenuInit();                 /* the game's: CommonMenuExit, its test hook */
    main_refresh();
    run_begin();
}

/* Each frame of the main menu (onDraw): the TV shows a match when one is live (SWOS 2020's
 * drawTvIcon) -- ModApi_LiveAny asks in the background at most every 20 s. */
static void mainDraw(void)
{
#ifdef SWOS_NETPLAY
    const int32_t live = ModApi_LiveAny() == 1;
    if (live != s_liveShown) {
        s_liveShown = live;
        ModApi_SetMenuImage(Main_liveCast, live ? "grafs_2020/menus2020/tv_live.bmp"
                                                : "grafs_2020/menus2020/tv_idle.bmp");
    }
#endif
}

/* Back on the main menu (onReturn): the logo as chosen, the layout, the TV. */
static void mainDrawn(void)
{
    logo_as_chosen();
    {   /* Back without this menu's layout -- a way to the main menu that unpacked it again and ran only
         * the game's CommonMenuExit (LOAD on CONTINUE's row, but at its own x: Playa, 2026-11-22) --
         * laid out again. */
        const MenuEntry *cont = entry(Main_continue), *load = entry(Main_load);
        if (cont->isInvisible && load->x != (uint16_t)(cont->x + (cont->dwWidth - load->dwWidth) / 2))
            main_refresh();
    }
    mainDraw();
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
GAME_BUTTON(multiplayer, MultiplayerMenuSelected)
GAME_BUTTON(options, OptionsMenuSelected)
GAME_BUTTON(continueContest, StartContest)
GAME_BUTTON(saveContest, SaveContest)
GAME_BUTTON(loadContest, LoadOldCompetitionMenu)
GAME_BUTTON(credits, CreditsMenuSelected)
GAME_BUTTON(classicCareer, CareerSeasons_Classic)     /* (src/career_seasons.c) */
GAME_BUTTON(worldCareer, CareerSeasons_World)
#undef GAME_BUTTON

/* ======================================================================================
 * REPLAYS (swos-2020 replaysMain.mnu; replays/replaysMenu.cpp): SWOS GALORE (the galore2020 mod's,
 * shown while it is on), HIGHLIGHTS (the game's own: VIEW / LOAD / SAVE), MATCH REPLAYS (a whole
 * match's .SRP or .RPL, chosen from the save folder, in the highlights viewer), REPLAY CONTROLS (the
 * viewer's keys, replayControls.mnu), and under them what the chosen one does.
 * ====================================================================================== */
extern uint8_t   _highlightsMenu[];
extern uint8_t   _selectedFilename[];
extern uintptr_t _extension;
extern int16_t   GetFilenameAndExtension(uint32_t preferredExt, uint8_t *defaultFilename, uint8_t *menuTitleStr);
extern void      ShowHighlights(void);
extern int32_t   ReplayV2_Load(const uint8_t *name);
extern void      ReplayV2_Unload(void);
extern void      DosReplay_ClearRpl(void);
extern int       DosReplay_LoadRpl(uint8_t *filename);

static int32_t galore_entry(void) { return ModApi_FindEntry(MODPOINT_MODES, "galore2020"); }

static void replaysMainMenu_OnInit(void)
{
    if (galore_entry() < 0) {           /* (no SWOS Galore: HIGHLIGHTS first) */
        show(ReplaysMainMenu_swosGalore, 0);
        nav(ReplaysMainMenu_highlights, -1, -1, -1, ReplaysMainMenu_matchReplays);
        SetCurrentEntry(ReplaysMainMenu_highlights);
    }
}

static void replaysMainMenu_OnDraw(void)
{
    const int32_t m = ModApi_MenuCurrentEntry();
    ModApi_MenuText(ReplaysMainMenu_desc, m == ReplaysMainMenu_highlights ? "GO TO THE HIGHLIGHTS MENU TO PROCEED"
                                       : m == ReplaysMainMenu_swosGalore ? "RATE AND CHART THE HIGHLIGHTS ONLINE"
                                       : m == ReplaysMainMenu_matchReplays ? "VIEW AND SAVE THE FULL LENGTH MATCH REPLAYS"
                                       : m == ReplaysMainMenu_replayControls ? "VIEW INSTRUCTION OF REPLAY CONTROLS"
                                       : "BACK TO THE PREVIOUS MENU");
}

static void swosGalore_OnSelect(void)
{
    const int32_t i = galore_entry();
    if (i >= 0)
        ModApi_RunEntry(MODPOINT_MODES, i);
}

static void highlights_OnSelect(void) { ShowMenu(_highlightsMenu); }

static void matchReplays_OnSelect(void)
{
    static uint8_t title[] = "MATCH REPLAYS";
    int16_t r;
    FileIo_ListOnly(0x2E535250u, 0x2E52504Cu);      /* '.SRP' (ours), '.RPL' (the PC's, SWOS 2020's) */
    r = GetFilenameAndExtension(0, title, title);
    FileIo_ListOnly(0, 0);
    if (r != 0)
        return;
    DosReplay_ClearRpl();
    ReplayV2_Unload();
    if ((_extension == 0x2E535250u ? ReplayV2_Load(_selectedFilename) : DosReplay_LoadRpl(_selectedFilename)) != 0) {
        ModApi_ShowError("NOT A MATCH REPLAY");
        return;
    }
    ShowHighlights();
    DosReplay_ClearRpl();
    ReplayV2_Unload();
}

static void replayControls_OnSelect(void) { ModApi_ShowMenu("swos2020/replaysControlsMenu"); }

static void replaysControlsMenu_OnInit(void) {}
/* drawReplayControlsLayout(5, 23): the keyboard and the pad, at SWOS 2020's screen 5, 23 -- its
 * menus' entries 8 right of where they are imported, so placed by the header (92, 0 as imported) */
static void replaysControlsMenu_OnDraw(void)
{
    ModApi_DrawMapAtEntry(SLUG, "replay_controls_300x56_1x1_map", ReplaysControlsMenu_header, 5 - 8 - 92, 23, 0, 0,
                          300, 56, 0);
}

static void replays_register(void)      /* (at the mod's start: SWOS 2020's names, global) */
{
    static int32_t registered;
    if (!registered) {
        static const MenuSymbol kHandlers[] = {
            { "replaysMainMenu_OnInit", (void *)replaysMainMenu_OnInit, SYM_FUNC },
            { "replaysMainMenu_OnDraw", (void *)replaysMainMenu_OnDraw, SYM_FUNC },
            { "swosGalore_OnSelect", (void *)swosGalore_OnSelect, SYM_FUNC },
            { "highlights_OnSelect", (void *)highlights_OnSelect, SYM_FUNC },
            { "matchReplays_OnSelect", (void *)matchReplays_OnSelect, SYM_FUNC },
            { "replayControls_OnSelect", (void *)replayControls_OnSelect, SYM_FUNC },
            { "replaysControlsMenu_OnInit", (void *)replaysControlsMenu_OnInit, SYM_FUNC },
            { "replaysControlsMenu_OnDraw", (void *)replaysControlsMenu_OnDraw, SYM_FUNC },
        };
        registered = 1;
        MenuJson_RegisterSymbols(kHandlers, (int)(sizeof kHandlers / sizeof kHandlers[0]));
    }
}

static void replays(void)
{
    replays_register();
    ModApi_ShowMenu("swos2020/replaysMainMenu");
    CommonMenuExit();
    main_refresh();
}

/* QUIT: SWOS 2020's quit.mnu (showQuitMenu) -- QUIT TO <the system this build is for>, RETURN TO
 * GAME chosen -- asked first, as closing the window is not. */
enum { Quit_quit, Quit_return };
static int32_t s_quitChosen;

#if defined(SWOS_SWITCH_BUILD)
#define QUIT_TO "QUIT TO SWITCH"
#elif defined(SWOS_IOS_BUILD)
#define QUIT_TO "QUIT TO IOS"
#elif defined(__ANDROID__)
#define QUIT_TO "QUIT TO ANDROID"
#elif defined(SWOS_WASM_BUILD)
#define QUIT_TO "QUIT TO BROWSER"
#elif defined(__APPLE__)
#define QUIT_TO "QUIT TO MAC"
#elif defined(_WIN32) || defined(__CYGWIN__)
#define QUIT_TO "QUIT TO WINDOWS"
#else
#define QUIT_TO "QUIT TO LINUX"
#endif

/* quitMenuOnInit / quitGame / returnToGame: the screen fades to black and back in on QUIT's
 * screen, and to black again as it closes -- the main menu then fades in (menuFade). The game's
 * fade (src/fade.c). */
static void quitInit(void)
{
    extern void FadeOutToBlack(void);
    extern void FadeIn(void);
    extern void DrawMenu(void);
    ModApi_MenuText(Quit_quit, QUIT_TO);
    SetCurrentEntry(Quit_return);
    FadeOutToBlack();
    DrawMenu();                         /* (drawn, ready for the fade in) */
    FadeIn();
}

static void quitGame(void)
{
    extern void FadeOutToBlack(void);
    FadeOutToBlack();
    s_quitChosen = 1;
    SetExitMenuFlag();
}

static void returnToGame(void)
{
    extern void FadeOutToBlack(void);
    extern uint16_t _menuFade;
    FadeOutToBlack();
    SetExitMenuFlag();
    _menuFade = 1;
}

static void quit(void)
{
    s_quitChosen = 0;
    ModApi_ShowMenu(SLUG "/quitMenu");
    if (s_quitChosen)
        Platform_RequestQuit();
    CommonMenuExit();
    main_refresh();
}

/* ======================================================================================
 * COMPETITIONS (swos-2020 competition.mnu). Ordinals: its entries, in order.
 * ====================================================================================== */
enum { Comp_header, Comp_online, Comp_speedruns, Comp_diy, Comp_challenges, Comp_preset,
       Comp_career, Comp_exit };

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
    /* (CLASSIC CAREER MODE: the main menu's two careers now -- CLASSIC CAREER, WORLD 26/27
     *  CAREER; the row stays, hidden, so the ordinals do) */
    show(Comp_career, 0);
    nav(Comp_challenges, -1, Comp_preset, row2, Comp_exit);
    nav(Comp_preset, row3 == Comp_challenges ? Comp_challenges : -1, -1, Comp_diy, Comp_exit);
    nav(Comp_exit, -1, -1, row3, -1);
    SetCurrentEntry(Comp_diy);
}

/* competitionMenu_OnDraw: SWOS 2020's yellow arrows either side of CHALLENGES, pointing at it,
 * coming and going -- frame[frame[i]] of { 0, 1, 2, 3, 2, 1 }, which is that table again, a step
 * every 20 of its 70 frames a second. Its pictures (rightArrow / leftArrow, 10x7) are the mod's
 * pixmaps.txt, made on the player's machine by scripts/make_menus2020_icons.py; without it nothing
 * is drawn. Only while CHALLENGES is there (the challenges mod on). */
static void competitionDraw(void)
{
    static const int32_t kFrame[6] = { 0, 1, 2, 3, 2, 1 };
    int32_t x = 0, y = 0, w = 0, h = 0;
    if (entry(Comp_challenges)->isInvisible)
        return;
    const int32_t step = kFrame[kFrame[(ModApi_Ticks() / 290) % 6]];   /* (20 frames of its 70) */
    ModApi_MenuEntryRect(Comp_challenges, &x, &y, &w, &h);
    const int32_t dy = h / 2 - 4;
    ModApi_RestoreMenuBackground(x - 16, y + dy, 13, 7);
    ModApi_RestoreMenuBackground(x + w + 3, y + dy, 13, 7);
    ModApi_DrawMapAtEntry(SLUG, "rightArrow", Comp_challenges, -13 - step, dy, 0, 0, 10, 7, 0);
    ModApi_DrawMapAtEntry(SLUG, "leftArrow", Comp_challenges, w + 3 + step, dy, 0, 0, 10, 7, 0);
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

/* ======================================================================================
 * PRACTICE (swos-2020 practice.mnu): FRIENDLY MATCH; TEAM TRAINING, PENALTY SHOOTOUT and DEMO GAME
 * are OpenSWOS's practice mod's (practice.training / .shootout / .demo, named in the JSON), shown
 * while it is on. (MODES -- ours, the mods' entries -- hidden for good: obsolete, Playa 2026-11-15;
 * the entry stays in the menu so the ordinals do not move.)
 * ====================================================================================== */
enum { Prac_header, Prac_friendly, Prac_training, Prac_shootout, Prac_demo, Prac_help, Prac_exit,
       Prac_modes };

static void practiceInit(void)
{
    const int32_t practice = ModApi_FindEntry(MODPOINT_MODES, "practice") >= 0;
    const int32_t last = practice ? Prac_demo : Prac_friendly;

    show(Prac_training, practice);
    show(Prac_shootout, practice);
    show(Prac_demo, practice);
    show(Prac_modes, 0);
    nav(Prac_friendly, -1, -1, -1, practice ? Prac_training : Prac_exit);
    nav(Prac_demo, -1, -1, Prac_shootout, Prac_exit);
    nav(Prac_exit, -1, -1, last, -1);
}

static void friendly(void) { choose(FriendlyMenu); }
static void modes(void)    { choose(ModesMenuSelected); }

/* ======================================================================================
 * EDITING (editing.mnu) and EXTRAS (extrasEx.mnu)
 * ====================================================================================== */
enum { Extras_header, Extras_news, Extras_tips, Extras_streams, Extras_editTeams,
       Extras_editTactics, Extras_manual, Extras_help, Extras_exit };

static void set_text(int32_t ordinal, const char *text) { ModApi_MenuText(ordinal, text); }

static void extrasInit(void) { SetCurrentEntry(Extras_tips); }   /* (LATEST NEWS, the first, hidden) */

/* The line under the buttons: what the chosen one does (extrasExMenuOnDraw). */
static void extrasDraw(void)
{
    static const char *const kHelp[] = {
        [Extras_news] = "LATEST NEWS FROM SENSIBLESOCCER.DE",
        [Extras_tips] = "LINKS TO WRITTEN ARTICLES AND WEBSITES",
        [Extras_streams] = "LINKS TO YOUTUBE, TWITCH AND ETC.",
        [Extras_editTeams] = "EDIT ORIGINAL AND EDITED CUSTOM TEAMS",
        [Extras_editTactics] = "EDIT CUSTOM TACTICS",
        [Extras_manual] = "READ INSTRUCTION MANUAL",
    };
    const int32_t m = ModApi_MenuCurrentEntry();
    set_text(Extras_help, m >= Extras_news && m <= Extras_manual && kHelp[m] ? kHelp[m] : "BACK TO MAIN MENU");
}

static void editTeams(void)   { choose(EditCustomTeamMenu); }
static void editTactics(void) { choose(EditTacticsMenu); }
static void diskFiling(void)  { choose(SaveDiskFilingMenu); }

/* INSTRUCTION MANUAL: the one SWOS 2020 opens (manualExtras_OnSelect), in the browser. */
static void manual(void)
{
    Platform_OpenUrl("https://www.sensiblesoccer.de/swos2020/swos_96_97_manual.pdf");
}

/* ======================================================================================
 * TIPS AND REVIEWS, STREAMS AND VIDEOS (SWOS 2020's ticker.mnu, tickerMenu_*): the ticker mod's
 * news (its news.json, from sensiblesoccer.de's tips.txt) listed, ten a page -- the number, the
 * category (the text before its ':'), the text -- a row opening its link in the browser. Tips are
 * the items before the first "STREAM:" one, streams the rest after SWOS 2020's own first row.
 * Ordinals: menus2020/tickerMenu.json -- the heading row then ten, per column.
 * ====================================================================================== */
enum { kTickRows = 10, kTickItems = 64, kTickText = 160, kTickLink = 256 };
enum { Tick_header, Tick_no0, Tick_cate0 = Tick_no0 + kTickRows + 1, Tick_text0 = Tick_cate0 + kTickRows + 1,
       Tick_up = Tick_text0 + kTickRows + 1, Tick_down, Tick_page, Tick_exit };

static struct { char text[kTickText], link[kTickLink]; } s_tick[kTickItems];
static int32_t s_tickCount, s_tickStreams;      /* items; the first stream's (getStreamsTicker) */
static int32_t s_tickMode;                      /* 1 tips, 2 streams (g_TickerMode) */
static int32_t s_tickPage[3];
static char    s_tickNo[kTickRows][12], s_tickCate[kTickRows][24], s_tickRow[kTickRows][kTickText];
static char    s_tickPageText[32];

static void tick_add(const char *text, const char *link)
{
    if (s_tickCount >= kTickItems || !text[0])
        return;
    snprintf(s_tick[s_tickCount].text, kTickText, "%s", text);
    snprintf(s_tick[s_tickCount].link, kTickLink, "%s", link);
    for (char *c = s_tick[s_tickCount].text; *c; c++)      /* (the menu fonts are capitals) */
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    s_tickCount++;
}

/* SWOS 2020's own first stream (loadNewsStr), before the feed's first "STREAM:" item. */
static const char kOwnStream[] = "STREAM: SWOS.EU LIVE BROADCAST OF ONLINE MATCHES";
static const char kOwnLink[] = "https://swos.eu/";

static void tick_news(const char *text, const char *link, void *ctx)
{
    (void)ctx;
    if (s_tickStreams < 0 && (strncmp(text, "STREAM:", 7) == 0 || strncmp(text, "stream:", 7) == 0)) {
        s_tickStreams = s_tickCount;
        tick_add(kOwnStream, kOwnLink);
    }
    tick_add(text, link);
}

/* The ticker's news (src/mod_united_ticker.c: save/mods/ticker, else its own), each time a list
 * opens. */
static void tick_load(void)
{
    s_tickCount = 0;
    s_tickStreams = -1;
    UnitedTicker_News(tick_news, NULL);
    if (s_tickStreams < 0) {
        s_tickStreams = s_tickCount;
        tick_add(kOwnStream, kOwnLink);
    }
}

/* The list's i-th item, or -1. */
static int32_t tick_item(int32_t i)
{
    const int32_t first = s_tickMode == 1 ? 0 : s_tickStreams;
    const int32_t last = s_tickMode == 1 ? s_tickStreams : s_tickCount;
    return i >= 0 && first + i < last ? first + i : -1;
}

static int32_t tick_pages(void)
{
    const int32_t n = s_tickMode == 1 ? s_tickStreams : s_tickCount - s_tickStreams;
    return n > 0 ? (n + kTickRows - 1) / kTickRows : 1;
}

static void tickerInit(void)
{
    tick_load();
    if (s_tickPage[s_tickMode] >= tick_pages())
        s_tickPage[s_tickMode] = 0;
}

/* shortenNameEx: the text up to the letter that takes it past `limit` pixels, then "...". */
static void tick_shorten(char *s, int32_t cap, int32_t limit)
{
    char probe[kTickText];
    const int32_t len = (int32_t)strlen(s);
    for (int32_t i = 0; i < len && i + 1 < (int32_t)sizeof probe; i++) {
        memcpy(probe, s, (size_t)(i + 1));
        probe[i + 1] = 0;
        if (ModApi_TextWidth(probe, 1) > limit) {
            if (i + 3 < cap)
                memcpy(s + i, "...", 4);
            return;
        }
    }
}

/* tickerMenu_OnDraw, for its TIPS AND REVIEWS / STREAMS AND VIDEOS lists. */
static void tickerDraw(void)
{
    enum { kGray = 7, kHeaderColour = 4, kLightBlue = 13 };     /* (kLightBrownWithOrangeFrame) */
    const int32_t sel = ModApi_MenuCurrentEntry();
    int32_t chosen = -1;
    set_text(Tick_header, s_tickMode == 1 ? "TIPS AND REVIEWS" : "STREAMS AND VIDEOS");
    set_text(Tick_no0, "NO");
    set_text(Tick_cate0, "CATEGORY");
    set_text(Tick_text0, "                         LINK COLLECTION");
    /* Its columns for the link lists: CATEGORY at 24, 50 wide, the text at 76, 234 wide -- SWOS
     * 2020's screen x, where NO (ticker.mnu's x -8, left as it is) is at 0. */
    for (int32_t r = 0; r <= kTickRows; r++) {
        const MenuEntry *no = entry(Tick_no0 + r);
        MenuEntry *cate = entry(Tick_cate0 + r), *text = entry(Tick_text0 + r);
        cate->x = (uint16_t)(no->x + 24);
        cate->dwWidth = 50;
        text->x = (uint16_t)(no->x + 76);
        text->dwWidth = 234;
    }
    ModApi_MenuColour(Tick_no0, kHeaderColour);
    ModApi_MenuColour(Tick_cate0, kHeaderColour);
    ModApi_MenuColour(Tick_text0, kHeaderColour);
    for (int32_t r = 0; r < kTickRows; r++)            /* (getCurrentTicker: any cell of the row) */
        if (sel == Tick_no0 + 1 + r || sel == Tick_cate0 + 1 + r || sel == Tick_text0 + 1 + r)
            chosen = r;
    for (int32_t r = 0; r < kTickRows; r++) {
        const int32_t it = tick_item(s_tickPage[s_tickMode] * kTickRows + r);
        const int32_t colour = r == chosen ? kLightBlue : kGray;
        s_tickNo[r][0] = 0;
        s_tickCate[r][0] = 0;
        s_tickRow[r][0] = 0;
        if (it >= 0) {
            const char *t = s_tick[it].text, *colon = strchr(t, ':');
            snprintf(s_tickNo[r], sizeof s_tickNo[r], "%d", (int)(s_tickPage[s_tickMode] * kTickRows + r + 1));
            if (colon) {
                snprintf(s_tickCate[r], sizeof s_tickCate[r], "%.*s", (int)(colon - t), t);
                t = colon + 1;
            }
            while (*t == ' ')
                t++;
            snprintf(s_tickRow[r], sizeof s_tickRow[r], "%s", t);
            tick_shorten(s_tickRow[r], (int32_t)sizeof s_tickRow[r], 295 - 26 - 62 + 56 - 14);
        }
        set_text(Tick_no0 + 1 + r, s_tickNo[r]);
        set_text(Tick_cate0 + 1 + r, s_tickCate[r]);
        set_text(Tick_text0 + 1 + r, s_tickRow[r]);
        ModApi_MenuColour(Tick_no0 + 1 + r, colour);
        ModApi_MenuColour(Tick_cate0 + 1 + r, colour);
        ModApi_MenuColour(Tick_text0 + 1 + r, colour);
        /* rows 1-5 go right to the page up, 6-10 to the page down */
        nav(Tick_text0 + 1 + r, -1, r < kTickRows / 2 ? Tick_up : Tick_down, r ? Tick_text0 + r : -1,
            r + 1 < kTickRows ? Tick_text0 + 2 + r : Tick_exit);
    }
    snprintf(s_tickPageText, sizeof s_tickPageText, "(%d OF %d)", (int)s_tickPage[s_tickMode] + 1, (int)tick_pages());
    set_text(Tick_page, s_tickPageText);
    nav(Tick_up, Tick_text0 + 1, -1, -1, Tick_down);
    nav(Tick_down, Tick_text0 + kTickRows, -1, Tick_up, Tick_exit);
    nav(Tick_exit, -1, Tick_down, Tick_text0 + kTickRows, -1);
}

/* A link from the news feed is someone else's text: only a web address is opened (Platform_OpenUrl
 * is ShellExecute "open" on Windows, which would also run a local path or a \\host\share program). */
static int32_t is_web_link(const char *link)
{
    return strncasecmp(link, "http://", 7) == 0 || strncasecmp(link, "https://", 8) == 0;
}

static void tickerOpen(void)
{
    const int32_t r = ModApi_MenuCurrentEntry() - (Tick_text0 + 1);
    const int32_t it = r >= 0 && r < kTickRows ? tick_item(s_tickPage[s_tickMode] * kTickRows + r) : -1;
    if (it < 0 || !s_tick[it].link[0])
        return;
    if (!is_web_link(s_tick[it].link)) {
        fprintf(stderr, "[menus2020] news link not opened (not http/https): %.80s\n", s_tick[it].link);
        return;
    }
    Platform_OpenUrl(s_tick[it].link);
}

/* tickerScrollUp_onSelect / tickerScrollDown_onSelect: round from the first page to the last. */
static void tickerUp(void)
{
    if (--s_tickPage[s_tickMode] < 0)
        s_tickPage[s_tickMode] = tick_pages() - 1;
}

static void tickerDown(void)
{
    if (++s_tickPage[s_tickMode] >= tick_pages())
        s_tickPage[s_tickMode] = 0;
}

static void tick_list(int32_t mode)
{
    s_tickMode = mode;
    ModApi_ShowMenu(SLUG "/tickerMenu");
}

static void tips(void)    { tick_list(1); }
static void streams(void) { tick_list(2); }

/* ======================================================================================
 * LIVE CAST: the live online matches (SWOS 2020 sent its TV to swos.eu/rpl in a browser; here
 * they are watched in the game, OpenSWOS's viewer ~3 s behind the players: ModApi_LiveList /
 * ModApi_LiveWatch, docs/NETPLAY.md §7). Ordinals: menus2020/liveMenu.json.
 * ====================================================================================== */
#ifdef SWOS_NETPLAY
enum { kLiveRows = 8 };
enum { Live_header, Live_first, Live_status = Live_first + kLiveRows, Live_refresh, Live_exit };

static ModLiveMatch s_live[kLiveRows];
static int32_t      s_liveCount;
static int32_t      s_liveAction;       /* what the list chose: 0 leave, 1 watch, 2 refresh */
static uint64_t     s_watchId;
static char         s_liveRow[kLiveRows][80];

static void liveInit(void)
{
    for (int32_t i = 0; i < kLiveRows; i++) {
        if (i < s_liveCount) {
            const ModLiveMatch *m = &s_live[i];
            if (m->minute >= 0)         /* (the score and the minute, when the server says them) */
                snprintf(s_liveRow[i], sizeof s_liveRow[i], "%s V %s (%s %d-%d %s) %d'",
                         m->players[0], m->players[1], m->teams[0], (int)m->goals[0], (int)m->goals[1],
                         m->teams[1], (int)m->minute);
            else
                snprintf(s_liveRow[i], sizeof s_liveRow[i], "%s V %s (%s V %s)",
                         m->players[0], m->players[1], m->teams[0], m->teams[1]);
            ModApi_MenuText(Live_first + i, s_liveRow[i]);
        }
        ModApi_MenuVisible(Live_first + i, i < s_liveCount);
        ModApi_MenuNav(Live_first + i, i ? Live_first + i - 1 : -1,
                       i + 1 < s_liveCount ? Live_first + i + 1 : Live_refresh);
    }
    ModApi_MenuText(Live_status, s_liveCount < 0 ? "THE SERVER CANNOT BE REACHED"
                                 : s_liveCount == 0 ? "NO MATCHES ARE LIVE RIGHT NOW"
                                 : "FIRE ON A MATCH TO WATCH IT");
    const int32_t last = s_liveCount > 0 ? Live_first + s_liveCount - 1 : -1;
    ModApi_MenuNav4(Live_refresh, -1, Live_exit, last, -1);
    ModApi_MenuNav4(Live_exit, Live_refresh, -1, last, -1);
    ModApi_MenuSelect(s_liveCount > 0 ? Live_first : Live_refresh);
}

static void liveWatch(void)
{
    const int32_t i = ModApi_MenuCurrentEntry() - Live_first;
    if (i < 0 || i >= s_liveCount)
        return;
    s_watchId = s_live[i].id;
    s_liveAction = 1;
    SetExitMenuFlag();
}

static void liveRefresh(void) { s_liveAction = 2; SetExitMenuFlag(); }

static void liveCast(void)
{
    for (;;) {
        s_liveCount = ModApi_LiveList(s_live, kLiveRows);
        s_liveAction = 0;
        if (ModApi_ShowMenu(SLUG "/liveMenu") != 0)
            break;
        if (s_liveAction == 1) {
            if (ModApi_LiveWatch(s_watchId) < 0)
                ModApi_ShowError("THAT MATCH IS NO LONGER LIVE");
        } else if (s_liveAction != 2) {
            break;
        }
    }
    CommonMenuExit();
    main_refresh();
}
#else
static void liveInit(void) {}
static void liveWatch(void) {}
static void liveRefresh(void) {}
static void liveCast(void) {}
#endif

static void init(void)
{
    static const ModHandler handlers[] = {
        { "mainInit", mainInit }, { "mainDrawn", mainDrawn }, { "mainDraw", mainDraw },
        { "liveCast", liveCast }, { "liveInit", liveInit }, { "liveWatch", liveWatch },
        { "liveRefresh", liveRefresh },
        { "compete", compete }, { "practice", practice }, { "replays", replays },
        { "options", options }, { "editing", editing }, { "extras", extras },
        { "continueContest", continueContest }, { "saveContest", saveContest },
        { "loadContest", loadContest }, { "quit", quit }, { "credits", credits },

        { "multiplayer", multiplayer },
        { "classicCareer", classicCareer }, { "worldCareer", worldCareer },
        { "competitionInit", competitionInit }, { "competitionDraw", competitionDraw },
        { "quitInit", quitInit }, { "quitGame", quitGame }, { "returnToGame", returnToGame },
        { "online", online }, { "diy", diy }, { "preset", preset }, { "career", career },
        { "speedruns", speedruns }, { "challenges", challenges },

        { "practiceInit", practiceInit },
        { "friendly", friendly }, { "modes", modes },

        { "extrasInit", extrasInit }, { "extrasDraw", extrasDraw }, { "tips", tips }, { "streams", streams },
        { "editTeams", editTeams }, { "editTactics", editTactics }, { "manual", manual },
        { "diskFiling", diskFiling },
        { "tickerInit", tickerInit }, { "tickerDraw", tickerDraw }, { "tickerOpen", tickerOpen },
        { "tickerUp", tickerUp }, { "tickerDown", tickerDown },
    };
    ModApi_RegisterHandlers(SLUG, handlers, (int32_t)(sizeof handlers / sizeof handlers[0]));
    replays_register();
}

/* The lobby, SWOS 2020's way: a change on the other side leaves this player's GO as it is -- a
 * confirmed line-up stays confirmed (Playaveli, 42e31392). Safe: the other player's own GO goes
 * down with any change to their side, so a match starts only on a side both games have. */
static int32_t keeps_ready(void) { return 1; }

const ModDef ModDef_menus2020 = {
    .slug            = SLUG,
    .init            = init,
    .menuDrawn       = run_menu_frame,
    .lobbyKeepsReady = keeps_ready,
};

#endif /* SWOS_SDL_BUILD */
