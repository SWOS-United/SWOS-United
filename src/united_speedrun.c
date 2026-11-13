/*
 * united_speedrun.c -- SWOS United's speedrun screens (fork mod "speedrun2020"): SWOS 2020's
 * SPEEDRUN as it looked there -- the eight targets, a target's difficulty (SELECT TEAMS, or one of
 * the five preconfigured pairs; YELLOW and RED CARD only the first), the LEADERBOARD (a style, a
 * target, a difficulty, ten places a page), VIEW MY RECORDS and LATEST RUNS -- its menus imported
 * (data_json/menus/swos2020/, the importer's "speedrun" group; LATEST RUNS is the log-in group's
 * newsMenu), their handlers here under SWOS 2020's names (game.cpp).
 *
 * The runs are OpenSWOS's speedrun mod's, shared: this mod draws them and asks it through its
 * "speedrun" service (targets, style, run), and it opens these screens in place of its own through
 * "speedrun-screens" (open). The boards come from the online-records mod (speedrun.board / mine /
 * news, SWOS 2020's swosapi_v3): without it, or offline, the screens say so.
 *
 * A time is FRAMES at its style's rate, as SWOS 2020 shows them (getFrameTimerString: m:ss.ss).
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD)

#include "menu_load_json.h"
#include "menu_system.h"
#include "platform_sdl.h"         /* PlatformInitOptions (the remembered log-in name) */
#include "swos2020_menus.h"
#include "united_ui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "speedrun2020"

extern void SetExitMenuFlag(void);
extern uint8_t _currentMenu[];
extern const char *United_UserName(void);

enum { kGray = 7, kLightBrownWithOrangeFrame = 4, kRed = 10, kPurple = 11, kGreen = 14, kYellowText = 15 };

static const char *const k_targets[8] = { "1 GOAL", "HEADER", "3 GOALS", "TACKLE", "10 GOALS", "YELLOW CARD",
                                          "MATCH", "RED CARD" };
static const char *const k_difficulties[5] = { "BEGINNER", "AMATEUR", "ADVANCED", "PRO", "MASTER" };

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }
static int32_t selected_ordinal(void)
{
    const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
    return sel ? (int32_t)sel->ordinal : -1;
}
static void set_text(MenuEntry *e, const char *text) { e->fg = (uintptr_t)text; }
static void set_back(MenuEntry *e, uint16_t color)
{
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = color;
}
static void set_color(MenuEntry *e, uint16_t color) { e->stringFlags = (uint16_t)((e->stringFlags & ~0x0F) | color); }

static void show(const char *name)
{
    uint8_t *menu = MenuJson_Load(name);
    if (menu)
        ShowMenu(menu);
    else
        fprintf(stderr, "[speedrun2020] cannot show %s\n", name);
}

static int32_t is_card(int32_t target) { return target == 5 || target == 7; }

/* SWOS 2020's game_style and its rate (getFrameTimerString) */
static int32_t style_fps(int32_t style) { return style == 0 ? 70 : style == 3 || style == 4 ? 60 : 50; }
static const char *style_name(int32_t style)
{
    return style == 0 ? "PC DOS" : style == 3 ? "60 FPS" : style == 4 ? "XBOX" : "AMIGA";
}

/* the gameplay style being played: SWOS 2020's number (OpenSWOS's speedrun mod names it) */
static int32_t current_style(void)
{
    char out[64] = "";
    if (ModApi_CallService("speedrun", "style", "{}", out, sizeof out) != 0)
        return 1;
    return !strncmp(out, "DOS", 3) ? 0 : !strncmp(out, "XBOX", 4) ? 4 : 1;
}

static void frame_time(char *out, uint32_t cap, int32_t frames, int32_t style)
{
    if (frames <= 0 || frames >= 99999) {
        snprintf(out, cap, "-");
        return;
    }
    const double t = (double)frames / style_fps(style);
    const int32_t whole = (int32_t)t;
    snprintf(out, cap, "%d:%05.2f", (int)(whole / 60), (whole % 60) + (t - whole));
}

/* the next tab-separated field; the line moves on */
static char *field(char **p)
{
    char *start = *p;
    if (!start)
        return (char *)"";
    char *tab = strpbrk(start, "\t\n");
    if (tab) {
        const char was = *tab;
        *tab = 0;
        *p = was == '\t' ? tab + 1 : NULL;
    } else {
        *p = NULL;
    }
    return start;
}

static void arrow_AfterDraw(MenuEntry *e);
static int32_t s_downArrows[3];         /* (the scroll-down entries of the three lists) */
static void arrow_AfterDraw(MenuEntry *e)
{
    const int32_t down = e->ordinal == s_downArrows[0] || e->ordinal == s_downArrows[1] || e->ordinal == s_downArrows[2];
    United_DrawPageArrow(down, e->x + 2, e->y + 1);
}

static char s_me[40];
static const char *me_upper(void)
{
    extern PlatformInitOptions *g_arcadeOpts;
    const char *u = United_UserName();
    uint32_t i = 0;
    /* (not logged in this session: the remembered log-in name, as the boards are read for it --
     *  SWOS 2020 highlighted its logged-in player) */
    if ((!u || !u[0]) && g_arcadeOpts && g_arcadeOpts->netplay_remember)
        u = g_arcadeOpts->netplay_name;
    for (; u && u[i] && i + 1 < sizeof s_me; i++)
        s_me[i] = (char)(u[i] >= 'a' && u[i] <= 'z' ? u[i] - 32 : u[i]);
    s_me[i] = 0;
    return s_me;
}

/* SWOS 2020's drawMapEx(map, x, y, ...) on the shown menu: its pictures are the speedrun mod's
 * (bin/mods/speedrun/pixmaps.txt), x, y SWOS 2020's screen -- its menus' entries 8 right of where
 * they are imported here, so 8 less -- placed by the menu's first entry, at s_picX0, 0 as imported
 * (where it is drawn has the screen's centring in it). From an onDraw that set s_picX0. */
static int32_t s_picX0;
static void pic(const char *name, int32_t sx, int32_t sy, int32_t ox, int32_t oy, int32_t w, int32_t h)
{
    ModApi_DrawMapAtEntry("speedrun", name, 0, sx - 8 - s_picX0, sy, ox, oy, w, h, 0);
}

/* ---- SPEEDRUN: the targets ---- */

static int32_t s_target, s_style;
static char s_mainHeader[40];

static void speedrunMainMenuOnInit(void)
{
    s_style = current_style();
    SetCurrentEntry(SpeedrunMainMenu_oneGoal);     /* (SWOS 2020's: the first target) */
}
static void speedrunMainMenuOnReturn(void) {}
static void animateBalls(void) {}

static void speedrunMainMenuOnDraw(void)
{
    static const char *const help[8] = {
        "SCORE A GOAL AS QUICKLY AS POSSIBLE", "SCORE WITH A HEADER AS QUICKLY AS POSSIBLE",
        "SCORE 3 GOALS AS QUICKLY AS POSSIBLE", "SCORE WITH A TACKLE AS QUICKLY AS POSSIBLE",
        "SCORE 10 GOALS AS QUICKLY AS POSSIBLE", "GET A YELLOW CARD AS QUICKLY AS POSSIBLE",
        "WIN A MATCH AS QUICKLY AS POSSIBLE", "GET A RED CARD AS QUICKLY AS POSSIBLE",
    };
    static const int32_t order[8] = { SpeedrunMainMenu_oneGoal, SpeedrunMainMenu_headerGoal, SpeedrunMainMenu_threeGoals,
                                       SpeedrunMainMenu_tackleGoal, SpeedrunMainMenu_tenGoals, SpeedrunMainMenu_yellowCard,
                                       SpeedrunMainMenu_matchGoal, SpeedrunMainMenu_redCard };
    const int32_t sel = selected_ordinal();
    const char *line = "";
    snprintf(s_mainHeader, sizeof s_mainHeader, "SPEEDRUN - %s", style_name(s_style));
    set_text(entry_at(SpeedrunMainMenu_speedrunMainHeader), s_mainHeader);
    for (int32_t t = 0; t < 8; t++)
        if (sel == order[t])
            line = help[t];
    if (sel == SpeedrunMainMenu_myRecords)
        line = "VIEW MY RECORDS";
    else if (sel == SpeedrunMainMenu_news)
        line = "CHECK OUT THE LATEST RUNS AND RECORDS";
    set_text(entry_at(SpeedrunMainMenu_HelpMsg), line);

    /* speedrunMainMenuOnDraw's pictures: a ball by 1, 3 and 10 GOALS, a tick by MATCH; the header,
     * the tackle and the two cards by theirs; the trophies by MY RECORDS */
    s_picX0 = 102;                      /* (speedrunMainHeader, as imported) */
    for (int32_t k = 0; k < 3; k++)
        pic("ball_8x8_5x1_Map", 27, 35 + 27 * k, 0, 0, 8, 8);
    pic("check_12x13_2x1_Map", 27, 35 + 27 * 3, 0, 0, 12, 13);
    pic("header_and_tackle_16x22_2x1_Map", 285, 35 + 27 * 0, 0, 0, 16, 22);
    pic("header_and_tackle_16x22_2x1_Map", 285, 35 + 27 * 1, 16, 0, 16, 22);
    pic("cards_24x15_5x1_Map", 285, 35 + 27 * 2, 0, 0, 24, 15);
    pic("cards_24x15_5x1_Map", 285, 35 + 27 * 3, 24, 0, 24, 15);
    pic("trophiesMap", 105 - 70, 147, 0, 14 * 8, 16, 14);
    pic("trophiesMap", 202 - 70, 147, 0, 14 * 0, 16, 14);
}

static void pick_target(int32_t t)
{
    s_target = t;
    show(is_card(t) ? "swos2020/selectSpeedrunDifficultyCardMenu" : "swos2020/selectSpeedrunDifficultyMenu");
}

static void oneGoal_OnSelect(void) { pick_target(0); }
static void headerGoal_OnSelect(void) { pick_target(1); }
static void threeGoals_OnSelect(void) { pick_target(2); }
static void tackleGoal_OnSelect(void) { pick_target(3); }
static void tenGoals_OnSelect(void) { pick_target(4); }
static void yellowCard_OnSelect(void) { pick_target(5); }
static void matchGoal_OnSelect(void) { pick_target(6); }
static void redCard_OnSelect(void) { pick_target(7); }
static void myRecords_OnSelect(void) { show("swos2020/viewMyRecordsMenu"); }
static void newsMain_OnSelect(void) { show("swos2020/newsMenu"); }
static void speedrunMainExit_OnSelect(void) { SetExitMenuFlag(); }

/* ---- a target's difficulty ---- */

static char s_diffHeader[40];

static void play(int32_t difficulty)
{
    char args[64], out[32];
    snprintf(args, sizeof args, "{\"target\":%d,\"difficulty\":%d}", (int)s_target, (int)difficulty);
    ModApi_CallService("speedrun", "run", args, out, sizeof out);
}

static void stars_AfterDraw(MenuEntry *e)       /* SWOS 2020's: right of each preconfigured button */
{
    const int32_t d = (int32_t)e->ordinal - SelectSpeedrunDifficultyMenu_beginner;
    if (d >= 0 && d < 5)
        United_DrawStars(d + 1, e->x + e->dwWidth + 10, e->y + 4);
}

static void selectSpeedrunDifficultyMenuOnInit(void)
{
    snprintf(s_diffHeader, sizeof s_diffHeader, "%s - DIFFICULTY", k_targets[s_target & 7]);
    set_text(entry_at(SelectSpeedrunDifficultyMenu_selectSpeedrunDifficultyHeader), s_diffHeader);
    for (int32_t d = 0; d < 5; d++)
        entry_at(SelectSpeedrunDifficultyMenu_beginner + d)->afterDrawCb = stars_AfterDraw;
}
static void selectSpeedrunDifficultyMenuOnDraw(void)
{
    selectSpeedrunDifficultyMenuOnInit();
    /* its trophies by the five (the stars are the buttons' afterDraws), and FREE TEAM CHOICE's
     * arrow under SELECT TEAMS */
    static const int8_t kTrophy[5] = { 1, 3, 5, 4, 8 };
    s_picX0 = 72;                       /* (selectSpeedrunDifficultyHeader, as imported) */
    for (int32_t k = 0; k < 5; k++) {
        const int32_t t = kTrophy[k], shift = t == 1 || t == 3;
        pic("trophiesMap", 94 + 70 + 2 * shift, 47 + 27 * k - 2 * shift, 0, 14 * t, 16, 14);
    }
    pic("arrows_default_scaled_Map", 40 + 42, 44 + 20, 0, 0, 24, 24);
}
static void selectSpeedrunTeams_OnSelect(void) { play(-1); }
static void beginner_OnSelect(void) { play(0); }
static void amateur_OnSelect(void) { play(1); }
static void advanced_OnSelect(void) { play(2); }
static void pro_OnSelect(void) { play(3); }
static void master_OnSelect(void) { play(4); }
static void selectSpeedrunDifficultyExit_OnSelect(void) { SetExitMenuFlag(); }

static int32_t s_lbStyle, s_lbMode, s_lbDiff, s_lbPage;
static void open_leaderboard(int32_t mode)
{
    s_lbStyle = current_style();
    s_lbMode = mode;
    s_lbDiff = 0;
    s_lbPage = 0;
    show("swos2020/leaderboardMenu");
}
static void newsDiff_OnSelect(void) { open_leaderboard(s_target); }

static void selectSpeedrunDifficultyCardMenu_OnInit(void)
{
    snprintf(s_diffHeader, sizeof s_diffHeader, "%s - DIFFICULTY", k_targets[s_target & 7]);
    set_text(entry_at(SelectSpeedrunDifficultyCardMenu_selectSpeedrunDifficultyHeader), s_diffHeader);
}
static void selectSpeedrunDifficultyCardMenu_OnDraw(void) { selectSpeedrunDifficultyCardMenu_OnInit(); }
static void selectSpeedrunTeamsCard_OnSelect(void) { play(-1); }
static void newsDiffCard_OnSelect(void) { open_leaderboard(s_target); }
static void selectSpeedrunDifficultyCardExit_OnSelect(void) { SetExitMenuFlag(); }

/* ---- the LEADERBOARD ---- */

enum { kBoardRows = 10 };
static char  s_board[65536];
static int32_t s_boardState = -1;       /* 0 here, 1 waiting, -1 none */
static char  s_lbStyleText[40], s_lbModeText[24], s_lbPageText[24];
static char  s_lbCells[kBoardRows][5][24];
static char  s_lbFiles[kBoardRows][96];  /* each row's best run's replay ("....RPL"), "" none */
static int32_t s_lbFresh;

/* SWOS 2020's order through the targets: down the left column, then the right */
static int32_t next_mode(int32_t m, int32_t step)
{
    static const int32_t order[8] = { 0, 2, 4, 6, 1, 3, 5, 7 };
    int32_t i = 0;
    while (i < 8 && order[i] != m)
        i++;
    return order[(i + 8 + step) % 8];
}

static void leaderboard_fetch(void)
{
    char args[96];
    snprintf(args, sizeof args, "{\"style\":%d,\"target\":%d%s}", (int)s_lbStyle, (int)s_lbMode,
             s_lbFresh ? ",\"fresh\":true" : "");
    s_lbFresh = 0;
    s_boardState = ModApi_CallService("online-records", "speedrun.board", args, s_board, sizeof s_board);
}

static void lb_style(int32_t step);
static int32_t play_replay(const char *file, int32_t style);
static void lb_mode(int32_t step);
static void lb_diff(int32_t step);
static void leaderboardMenuOnInit(void) { s_boardState = 1; s_lbFresh = 0; }
static void leaderboardMenuOnReturn(void) {}

static void leaderboardMenuOnDraw(void)
{
    static const char *const heads[5] = { "RANK", "DATE", "PLAYER", "PLAYED", "BEST" };
    static const int32_t col0[5] = { LeaderboardMenu_ranking_00, LeaderboardMenu_date_00, LeaderboardMenu_username_00,
                                     LeaderboardMenu_played_00, LeaderboardMenu_best_00 };
    const int32_t diff = is_card(s_lbMode) ? 0 : s_lbDiff;
    const char *me = me_upper();
    if (s_boardState == 1)
        leaderboard_fetch();

    snprintf(s_lbStyleText, sizeof s_lbStyleText, "LEADERBOARD - %s", style_name(s_lbStyle));
    set_text(entry_at(LeaderboardMenu_gameStyle), s_lbStyleText);
    snprintf(s_lbModeText, sizeof s_lbModeText, "%s", k_targets[s_lbMode & 7]);
    set_text(entry_at(LeaderboardMenu_gameMode), s_lbModeText);
    set_text(entry_at(LeaderboardMenu_difficulty), is_card(s_lbMode) ? "-" : k_difficulties[diff]);
    for (int32_t c = 0; c < 5; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }

    /* the rows of this difficulty, in place order */
    int32_t n = 0, shown = 0;
    for (int32_t r = 0; r < kBoardRows; r++)
    {
        for (int32_t c = 0; c < 5; c++)
            s_lbCells[r][c][0] = 0;
        s_lbFiles[r][0] = 0;
    }
    if (s_boardState == 0) {
        static char copy[65536];
        snprintf(copy, sizeof copy, "%s", s_board);
        for (char *line = copy; line && *line;) {
            char *next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            char *p = line;
            const int32_t d = atoi(field(&p));
            if (d == diff) {
                if (n >= s_lbPage * kBoardRows && shown < kBoardRows) {
                    char *rank = field(&p), *date = field(&p), *user = field(&p), *played = field(&p);
                    const int32_t frames = atoi(field(&p));
                    snprintf(s_lbFiles[shown], sizeof s_lbFiles[shown], "%s", field(&p));
                    snprintf(s_lbCells[shown][0], 24, "%s", rank);
                    snprintf(s_lbCells[shown][1], 24, "%s", date);
                    snprintf(s_lbCells[shown][2], 24, "%.14s", user);
                    snprintf(s_lbCells[shown][3], 24, "%s", played);
                    frame_time(s_lbCells[shown][4], 24, frames, s_lbStyle);
                    shown++;
                }
                n++;
            }
            line = next;
        }
    } else {
        snprintf(s_lbCells[0][2], 24, "%s", s_boardState == 1 ? "LOADING..." : "NOT AVAILABLE");
    }
    for (int32_t r = 0; r < kBoardRows; r++) {
        const int32_t mine = me[0] && !strcmp(s_lbCells[r][2], me);
        for (int32_t c = 0; c < 5; c++) {
            MenuEntry *e = entry_at(col0[c] + 1 + r);
            set_text(e, s_lbCells[r][c]);
            set_back(e, c == 0 ? kPurple : mine ? kRed : kGray);
        }
    }
    const int32_t pages = n ? (n + kBoardRows - 1) / kBoardRows : 1;
    if (s_lbPage >= pages)
        s_lbPage = pages - 1;
    snprintf(s_lbPageText, sizeof s_lbPageText, "(%d / %d)", (int)s_lbPage + 1, (int)pages);
    set_text(entry_at(LeaderboardMenu_leaderboard_Page), s_lbPageText);
    entry_at(LeaderboardMenu_leaderboard_ScrollUp)->afterDrawCb = arrow_AfterDraw;
    entry_at(LeaderboardMenu_leaderboard_ScrollDown)->afterDrawCb = arrow_AfterDraw;
    entry_at(LeaderboardMenu_makeRecordFrame)->isInvisible = 1;

    /* SWOS 2020's yellow arrows either side of the style, the target and the difficulty
     * (arrows_12x12_13x1_Map: left 6, right 2; its entries stay to be clicked) ... */
    static const int32_t ay[3] = { 1 + 4, 28 + 5 - 15 + 4, 28 + 5 + 4 }, axl[3] = { 95 - 10 + 2, 95 + 2, 95 + 2 },
                         axr[3] = { 215 + 10 - 2 + 1, 215 - 2 + 1, 215 - 2 + 1 };
    s_picX0 = 92;                       /* (gameStyle, as imported) */
    for (int32_t k = 0; k < 3; k++) {
        pic("arrows_12x12_13x1_Map", axl[k], ay[k], 12 * 6, 0, 12, 12);
        pic("arrows_12x12_13x1_Map", axr[k], ay[k], 12 * 2, 0, 12, 12);
    }
    /* ... and LEFT / RIGHT on one of the three steps it as its arrows do (SWOS 2020's
     * finalControlsStatus 6 / 2 in leaderboardMenuOnDraw) */
    {
        extern uint16_t _menuControlsDirection;
        const int32_t sel = selected_ordinal(), step = _menuControlsDirection == 2 ? 1 : _menuControlsDirection == 6 ? -1 : 0;
        if (step && sel == LeaderboardMenu_gameStyle)
            lb_style(step);
        else if (step && sel == LeaderboardMenu_gameMode)
            lb_mode(step);
        else if (step && sel == LeaderboardMenu_difficulty)
            lb_diff(step);
    }
    /* SWOS 2020's links (leaderboardMenuOnDraw, "menu navi"): the BEST column a list of its own,
     * reached from the difficulty and left for the scroll arrows -- the times to be chosen with the
     * keys and the pad, not the mouse alone (Playa) */
    for (int32_t r = 0; r < kBoardRows; r++) {
        const int32_t best = LeaderboardMenu_best_00 + 1 + r;
        ModApi_MenuNav4(best, -1, r < 5 ? LeaderboardMenu_leaderboard_ScrollUp : LeaderboardMenu_leaderboard_ScrollDown,
                        r ? best - 1 : LeaderboardMenu_difficulty, r + 1 < kBoardRows ? best + 1 : LeaderboardMenu_exit);
    }
    ModApi_MenuNav4(LeaderboardMenu_difficulty, -1, -1, LeaderboardMenu_gameMode, LeaderboardMenu_best_00 + 1);
    ModApi_MenuNav4(LeaderboardMenu_leaderboard_ScrollUp, LeaderboardMenu_best_00 + 1, -1, -1,
                    LeaderboardMenu_leaderboard_ScrollDown);
    ModApi_MenuNav4(LeaderboardMenu_leaderboard_ScrollDown, LeaderboardMenu_best_00 + kBoardRows, -1,
                    LeaderboardMenu_leaderboard_ScrollUp, LeaderboardMenu_exit);
    ModApi_MenuNav4(LeaderboardMenu_exit, LeaderboardMenu_difficulty_Dec, LeaderboardMenu_difficulty_Inc,
                    LeaderboardMenu_best_00 + kBoardRows, -1);

    /* SWOS_SPEEDRUN_REPLAY_TEST=1 (--menu=swos2020/leaderboardMenu): once the board is here, the
     * second place's run downloaded and played, then the game quits -- 0 when it played */
    {
        static int32_t tested;
        if (!tested && s_boardState == 0 && getenv("SWOS_SPEEDRUN_REPLAY_TEST")) {
            tested = 1;
            const int32_t rc = play_replay(s_lbFiles[1], s_lbStyle);
            fprintf(stdout, "SPEEDRUN REPLAY TEST %s: %s\n", rc == 0 ? "PASS" : "FAIL", s_lbFiles[1]);
            fflush(stdout);
            exit(rc == 0 ? 0 : 1);
        }
    }
}

static void lb_style(int32_t step)
{
    static const int32_t styles[4] = { 0, 1, 3, 4 };
    int32_t i = 0;
    while (i < 4 && styles[i] != s_lbStyle)
        i++;
    s_lbStyle = styles[(i + 4 + step) % 4];
    s_lbPage = 0;
    s_boardState = 1;
}
static void lb_mode(int32_t step)
{
    s_lbMode = next_mode(s_lbMode, step);
    s_lbPage = 0;
    s_boardState = 1;
}
static void lb_diff(int32_t step)
{
    s_lbDiff = (s_lbDiff + 5 + step) % 5;
    s_lbPage = 0;
}
static void gameStyle_onSelect(void) { lb_style(1); }
static void gameStyle_Dec_onSelect(void) { lb_style(-1); }
static void gameStyle_Inc_onSelect(void) { lb_style(1); }
static void gameMode_onSelect(void) { lb_mode(1); }
static void gameMode_Dec_onSelect(void) { lb_mode(-1); }
static void gameMode_Inc_onSelect(void) { lb_mode(1); }
static void difficulty_onSelect(void) { lb_diff(1); }
static void difficulty_Dec_onSelect(void) { lb_diff(-1); }
static void difficulty_Inc_onSelect(void) { lb_diff(1); }
/* A best time: that run's replay, as SWOS 2020's bestRecordInLeaderboard_OnSelect -- asked first,
 * downloaded from the records site (a .7z of its .RPL; the online-records service), played whole
 * in the highlights viewer at its own style's rate (the .RPL's game style). */
static void bestRecordInLeaderboard_OnSelect(void)
{
    static const char *const kYesExit[2] = { "YES", "EXIT" }, *const kOk[1] = { "OK" };
    const int32_t row = selected_ordinal() - (LeaderboardMenu_best_00 + 1);
    if (row < 0 || row >= kBoardRows)
        return;
    if (!s_lbFiles[row][0]) {
        ModApi_ShowDialog("SPEEDRUN REPLAY", "NO REPLAY FOR THIS PLACE", kOk, 1, 0);
        return;
    }
    if (ModApi_ShowDialog("SPEEDRUN REPLAY", "ARE YOU SURE TO REPLAY|THE RECORDED REPLAY FILE?", kYesExit, 2, 0) != 0)
        return;
    if (play_replay(s_lbFiles[row], s_lbStyle) != 0)
        ModApi_ShowDialog("SPEEDRUN REPLAY", "REPLAY FILE IS NOT AVAILABLE", kOk, 1, 0);
}

/* A board's run (its .RPL's name): downloaded, then played whole in the highlights viewer at the
 * board's style's rate (SWOS 2020's g_CustomFPS = the board's game style: the .RPL's own style
 * byte is not one of ours). 0 played. */
static int32_t play_replay(const char *file, int32_t style)
{
    extern void ShowHighlights(void);
    extern void DosReplay_ClearRpl(void);
    extern int  DosReplay_LoadRpl(uint8_t *filename);
    extern void ReplayV2_Unload(void);
    extern int  swos_io_save(const uint8_t *name, const uint8_t *buf, uint32_t size);
    char args[200], f[160];
    snprintf(args, sizeof args, "{\"file\":\"%s\"}", ModApi_JsonEscape(file, f, sizeof f));
    int32_t rc = 1;
    for (int32_t waited = 0; rc == 1 && waited < 30000; waited += 20) {
        rc = ModApi_CallService("online-records", "speedrun.replay", args, NULL, 0);
        if (rc == 1)
            SDL_Delay(20);
    }
    uint32_t size = 0;
    uint8_t *data = rc == 0 ? (uint8_t *)ModApi_LoadFile("speedrun2020", "run.RPL", &size) : NULL;
    const int32_t saved = data && swos_io_save((const uint8_t *)"SPEEDRUN.RPL", data, size) == 0;
    free(data);
    DosReplay_ClearRpl();
    ReplayV2_Unload();
    if (!saved || DosReplay_LoadRpl((uint8_t *)"SPEEDRUN.RPL") != 0)
        return -1;
    {
        extern int _replayPlaybackFps;
        _replayPlaybackFps = style_fps(style);
    }
    ShowHighlights();
    DosReplay_ClearRpl();
    return 0;
}
static void leaderboard_ScrollUp_onSelect(void) { if (s_lbPage > 0) s_lbPage--; }
static void leaderboard_ScrollDown_onSelect(void) { s_lbPage++; }
static void leaderboardExit_OnSelect(void) { SetExitMenuFlag(); }
static void makeRecordFrame_OnSelect(void) {}

/* ---- VIEW MY RECORDS ---- */

static int32_t s_mrMode, s_mrStyle, s_mineState, s_mineFresh;
static char  s_mine[16384], s_mrHeader[40], s_mrUser[48], s_mrModeText[24], s_mrHelp[64];
static char  s_mrCells[5][5][24];

static void viewMyRecordsMenuOnInit(void)
{
    s_mrStyle = current_style();
    s_mineState = 1;
}

static void viewMyRecordsMenuOnDraw(void)
{
    static const char *const heads[5] = { "DIFFICULTY", "PLAYED", "BEST", "CURRENT", "RANK" };
    static const int32_t col0[5] = { ViewMyRecordsMenu_difficulty_00, ViewMyRecordsMenu_played_00,
                                     ViewMyRecordsMenu_best_00, ViewMyRecordsMenu_current_00, ViewMyRecordsMenu_rank_00 };
    const char *me = me_upper();
    if (s_mineState == 1) {
        char args[64];
        snprintf(args, sizeof args, "{\"style\":%d%s}", (int)s_mrStyle, s_mineFresh ? ",\"fresh\":true" : "");
        s_mineFresh = 0;
        s_mineState = ModApi_CallService("online-records", "speedrun.mine", args, s_mine, sizeof s_mine);
    }
    snprintf(s_mrHeader, sizeof s_mrHeader, "VIEW MY RECORDS - %s", style_name(s_mrStyle));
    set_text(entry_at(ViewMyRecordsMenu_viewMyRecordsMenuHeader), s_mrHeader);
    if (me[0])
        snprintf(s_mrUser, sizeof s_mrUser, "USERNAME: %s", me);
    else
        snprintf(s_mrUser, sizeof s_mrUser, "OFFLINE MODE");
    set_text(entry_at(ViewMyRecordsMenu_recordMode), s_mrUser);
    snprintf(s_mrModeText, sizeof s_mrModeText, "%s", k_targets[s_mrMode & 7]);
    set_text(entry_at(ViewMyRecordsMenu_speedrunMode), s_mrModeText);

    for (int32_t c = 0; c < 5; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }
    for (int32_t d = 0; d < 5; d++) {
        snprintf(s_mrCells[d][0], 24, "%s", is_card(s_mrMode) && d ? "" : is_card(s_mrMode) ? "-" : k_difficulties[d]);
        snprintf(s_mrCells[d][1], 24, "0");
        snprintf(s_mrCells[d][2], 24, "-");
        snprintf(s_mrCells[d][3], 24, "-");
        snprintf(s_mrCells[d][4], 24, "%s", me[0] ? "-" : "X");
    }
    if (s_mineState == 0) {
        static char copy[16384];
        snprintf(copy, sizeof copy, "%s", s_mine);
        for (char *line = copy; line && *line;) {
            char *next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            char *p = line;
            const int32_t target = atoi(field(&p)), d = atoi(field(&p));
            if (target == s_mrMode && d >= 0 && d < 5) {
                const int32_t played = atoi(field(&p)), best = atoi(field(&p)), last = atoi(field(&p));
                const int32_t rank = atoi(field(&p));
                snprintf(s_mrCells[d][1], 24, "%d", (int)played);
                frame_time(s_mrCells[d][2], 24, best, s_mrStyle);
                frame_time(s_mrCells[d][3], 24, last, s_mrStyle);
                if (played)
                    snprintf(s_mrCells[d][4], 24, "%d", (int)rank);
            }
            line = next;
        }
    }
    for (int32_t d = 0; d < 5; d++)
        for (int32_t c = 0; c < 5; c++) {
            MenuEntry *e = entry_at(col0[c] + 1 + d);
            set_text(e, s_mrCells[d][c]);
            set_back(e, c == 0 ? kPurple : kGray);
        }
    /* the line under the table: online, what is happening; this machine's best, always */
    {
        char out[1024] = "", best[16] = "-";
        if (ModApi_CallService("speedrun", "targets", "{}", out, sizeof out) == 0) {
            char *line = out;
            for (int32_t t = 0; t < s_mrMode && line; t++) {
                line = strchr(line, '\n');
                if (line)
                    line++;
            }
            if (line) {
                char *p = line;
                field(&p); field(&p); field(&p); field(&p);
                snprintf(best, sizeof best, "%s", field(&p));
            }
        }
        if (s_mineState == 1)
            snprintf(s_mrHelp, sizeof s_mrHelp, "LOADING...");
        else
            snprintf(s_mrHelp, sizeof s_mrHelp, "BEST ON THIS MACHINE: %s", best);
        entry_at(ViewMyRecordsMenu_HelpMsg)->content = ENTRY_STRING;   /* (SWOS 2020's has no text) */
        entry_at(ViewMyRecordsMenu_HelpMsg)->y = (uint16_t)(entry_at(ViewMyRecordsMenu_exit)->y - 13);
        set_text(entry_at(ViewMyRecordsMenu_HelpMsg), s_mrHelp);
        set_color(entry_at(ViewMyRecordsMenu_HelpMsg), kYellowText);
    }
    entry_at(ViewMyRecordsMenu_reset)->isInvisible = 1;    /* (SWOS 2020 deleted the online rows) */
}

static void speedrunMode_Dec_onSelect(void) { s_mrMode = next_mode(s_mrMode, -1); }
static void speedrunMode_onSelect(void) { s_mrMode = next_mode(s_mrMode, 1); }
static void speedrunMode_Inc_onSelect(void) { s_mrMode = next_mode(s_mrMode, 1); }
static void bestRecord_OnSelect(void) {}
static void currentRecord_OnSelect(void) {}
static void rank_OnSelect(void) {}
static void viewMyRecordsExit_OnSelect(void) { SetExitMenuFlag(); }
static void viewMyRecordsSync_OnSelect(void) { s_mineState = 1; s_mineFresh = 1; }     /* UPDATE */
static void viewMyRecordsReset_OnSelect(void) {}
static void viewMyRecordsLeaderboard_OnSelect(void) { open_leaderboard(s_mrMode); }

/* ---- LATEST RUNS ---- */

enum { kNewsRows = 5 };
static char  s_news[65536];
static int32_t s_newsState, s_newsPage;
static char  s_newsCells[kNewsRows][7][24], s_newsPageText[24];

static void newsMenu_OnInit(void) { s_newsState = 1; s_newsPage = 0; }
static void newsMenu_OnReturn(void) {}

static void newsMenu_OnDraw(void)
{
    static const char *const heads[7] = { "DATE", "STYLE", "MODE", "DIFFI.", "PLAYER", "RECORD", "RANK" };
    static const int32_t col0[7] = { NewsMenu_date_00, NewsMenu_style_00, NewsMenu_mode_00, NewsMenu_diffi_00,
                                     NewsMenu_user_00, NewsMenu_best_00, NewsMenu_rank_00 };
    int32_t n = 0, shown = 0;
    if (s_newsState == 1)
        s_newsState = ModApi_CallService("online-records", "speedrun.news", "{}", s_news, sizeof s_news);
    set_text(entry_at(NewsMenu_header), "LATEST RUNS");
    for (int32_t c = 0; c < 7; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }
    for (int32_t r = 0; r < kNewsRows; r++)
        for (int32_t c = 0; c < 7; c++)
            s_newsCells[r][c][0] = 0;
    if (s_newsState == 0) {
        static char copy[65536];
        snprintf(copy, sizeof copy, "%s", s_news);
        for (char *line = copy; line && *line;) {
            char *next = strchr(line, '\n');
            if (next)
                *next++ = 0;
            if (n >= s_newsPage * kNewsRows && shown < kNewsRows) {
                char *p = line;
                char *date = field(&p);
                const int32_t style = atoi(field(&p)), mode = atoi(field(&p)), d = atoi(field(&p));
                char *user = field(&p);
                const int32_t frames = atoi(field(&p));
                char *rank = field(&p);
                snprintf(s_newsCells[shown][0], 24, "%s", date);
                snprintf(s_newsCells[shown][1], 24, "%s", style == 0 ? "DOS" : style == 3 ? "60FPS" : style == 4 ? "XBOX" : "AMIGA");
                snprintf(s_newsCells[shown][2], 24, "%s", mode == 5 ? "YELLOW." : mode == 7 ? "RED." : k_targets[mode & 7]);
                snprintf(s_newsCells[shown][3], 24, "%s", is_card(mode) ? "-" : k_difficulties[d >= 0 && d < 5 ? d : 0]);
                snprintf(s_newsCells[shown][4], 24, "%.10s", user);
                frame_time(s_newsCells[shown][5], 24, frames, style);
                snprintf(s_newsCells[shown][6], 24, "%s", rank);
                shown++;
            }
            n++;
            line = next;
        }
    } else {
        snprintf(s_newsCells[0][4], 24, "%s", s_newsState == 1 ? "LOADING..." : "NOT AVAILABLE");
    }
    for (int32_t r = 0; r < kNewsRows; r++)
        for (int32_t c = 0; c < 7; c++) {
            MenuEntry *e = entry_at(col0[c] + 1 + r);
            set_text(e, s_newsCells[r][c]);
            set_back(e, kGray);
        }
    const int32_t pages = n ? (n + kNewsRows - 1) / kNewsRows : 1;
    if (s_newsPage >= pages)
        s_newsPage = pages - 1;
    snprintf(s_newsPageText, sizeof s_newsPageText, "(%d / %d)", (int)s_newsPage + 1, (int)pages);
    set_text(entry_at(NewsMenu_news_Page), s_newsPageText);
    set_text(entry_at(NewsMenu_dateInfo), "");
    entry_at(NewsMenu_news_ScrollUp)->afterDrawCb = arrow_AfterDraw;
    entry_at(NewsMenu_news_ScrollDown)->afterDrawCb = arrow_AfterDraw;
    entry_at(NewsMenu_newsOnDiscord)->isInvisible = 1;
}

static void news_ScrollUp_onSelect(void) { if (s_newsPage > 0) s_newsPage--; }
static void news_ScrollDown_onSelect(void) { s_newsPage++; }
static void bestNewsMenu_OnSelect(void) {}
static void rankNewsMenu_OnSelect(void) {}
static void newsOnDiscordNewsMenu_OnSelect(void) {}

/* ---- the mod ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(speedrunMainMenuOnInit), HANDLER(speedrunMainMenuOnDraw), HANDLER(speedrunMainMenuOnReturn),
    HANDLER(animateBalls), HANDLER(oneGoal_OnSelect), HANDLER(headerGoal_OnSelect), HANDLER(threeGoals_OnSelect),
    HANDLER(tackleGoal_OnSelect), HANDLER(tenGoals_OnSelect), HANDLER(yellowCard_OnSelect), HANDLER(matchGoal_OnSelect),
    HANDLER(redCard_OnSelect), HANDLER(myRecords_OnSelect), HANDLER(newsMain_OnSelect), HANDLER(speedrunMainExit_OnSelect),
    HANDLER(selectSpeedrunDifficultyMenuOnInit), HANDLER(selectSpeedrunDifficultyMenuOnDraw),
    HANDLER(selectSpeedrunTeams_OnSelect), HANDLER(beginner_OnSelect), HANDLER(amateur_OnSelect),
    HANDLER(advanced_OnSelect), HANDLER(pro_OnSelect), HANDLER(master_OnSelect), HANDLER(newsDiff_OnSelect),
    HANDLER(selectSpeedrunDifficultyExit_OnSelect),
    HANDLER(selectSpeedrunDifficultyCardMenu_OnInit), HANDLER(selectSpeedrunDifficultyCardMenu_OnDraw),
    HANDLER(selectSpeedrunTeamsCard_OnSelect), HANDLER(newsDiffCard_OnSelect),
    HANDLER(selectSpeedrunDifficultyCardExit_OnSelect),
    HANDLER(leaderboardMenuOnInit), HANDLER(leaderboardMenuOnDraw), HANDLER(leaderboardMenuOnReturn),
    HANDLER(gameStyle_onSelect), HANDLER(gameStyle_Dec_onSelect), HANDLER(gameStyle_Inc_onSelect),
    HANDLER(gameMode_onSelect), HANDLER(gameMode_Dec_onSelect), HANDLER(gameMode_Inc_onSelect),
    HANDLER(difficulty_onSelect), HANDLER(difficulty_Dec_onSelect), HANDLER(difficulty_Inc_onSelect),
    HANDLER(bestRecordInLeaderboard_OnSelect), HANDLER(leaderboard_ScrollUp_onSelect),
    HANDLER(leaderboard_ScrollDown_onSelect), HANDLER(leaderboardExit_OnSelect), HANDLER(makeRecordFrame_OnSelect),
    HANDLER(viewMyRecordsMenuOnInit), HANDLER(viewMyRecordsMenuOnDraw), HANDLER(speedrunMode_Dec_onSelect),
    HANDLER(speedrunMode_onSelect), HANDLER(speedrunMode_Inc_onSelect), HANDLER(bestRecord_OnSelect),
    HANDLER(currentRecord_OnSelect), HANDLER(rank_OnSelect), HANDLER(viewMyRecordsExit_OnSelect),
    HANDLER(viewMyRecordsSync_OnSelect), HANDLER(viewMyRecordsReset_OnSelect),
    HANDLER(viewMyRecordsLeaderboard_OnSelect),
    HANDLER(newsMenu_OnInit), HANDLER(newsMenu_OnDraw), HANDLER(newsMenu_OnReturn), HANDLER(news_ScrollUp_onSelect),
    HANDLER(news_ScrollDown_onSelect), HANDLER(bestNewsMenu_OnSelect), HANDLER(rankNewsMenu_OnSelect),
    HANDLER(newsOnDiscordNewsMenu_OnSelect),
};
#undef HANDLER

/* "speedrun-screens": the speedrun mod opens these in place of its own */
static int32_t screens(const char *method, const char *args, char *out, uint32_t cap)
{
    (void)args; (void)out; (void)cap;
    if (strcmp(method, "open") != 0)
        return -1;
    show("swos2020/speedrunMainMenu");
    return 0;
}

static void speedrun2020_init(void)
{
    s_downArrows[0] = LeaderboardMenu_leaderboard_ScrollDown;
    s_downArrows[1] = NewsMenu_news_ScrollDown;
    s_downArrows[2] = -1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    ModApi_RegisterService(SLUG, "speedrun-screens", screens);
}

const ModDef ModDef_speedrun2020 = {
    .slug = SLUG,
    .init = speedrun2020_init,
};

#else
const ModDef ModDef_speedrun2020 = { .slug = "speedrun2020" };
#endif
