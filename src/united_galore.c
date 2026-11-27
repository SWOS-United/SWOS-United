/*
 * united_galore.c -- SWOS United's SWOS GALORE (fork mod "galore2020"): SWOS 2020's shared goal
 * highlights -- its menu (ALL TIME CHART, MONTHLY CHART, RATE HIGHLIGHTS, SHARE HIGHLIGHTS) and
 * its chart (seven a page: rank, date, player, comment, the average rating in stars), a pick
 * downloaded and shown in OpenSWOS's highlights viewer (ModApi_ViewHighlights, its scene alone).
 * Its menus imported (data_json/menus/swos2020/, the importer's "SWOS Galore" group), their
 * handlers here under SWOS 2020's names (replays/replaysMenu.cpp).
 *
 * The charts and the highlights come from sensiblesoccer.de (the online-records mod's galore.chart /
 * galore.download). RATE HIGHLIGHTS and SHARE HIGHLIGHTS write there, as SWOS 2020 did (Playa,
 * 2026-11-21: "replicate the functionalities"), after its SERVER LOGIN -- SWOS-API's client
 * (united_records.h, UnitedRecords_Galore*), on a thread of their own. REPLAYS -> SWOS GALORE.
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD)

#include "menu_load_json.h"
#include "menu_system.h"
#include "swos2020_menus.h"
#include "united_ui.h"
#include "endian_helpers.h"
#include "platform_sdl.h"      /* Platform_VBlank: a download waited for */
#include "text_render.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLUG "galore2020"

extern void SetExitMenuFlag(void);
extern uint8_t _currentMenu[];
extern uint16_t _fireResetFlag;

enum { kGray = 7, kLightBrownWithOrangeFrame = 4, kRed = 10, kPurple = 11, kYellowText = 15 };
enum { kRows = 7, kMax = 400 };

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }
static int32_t selected_ordinal(void)
{
    const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
    return sel ? (int32_t)sel->ordinal : -1;
}
static void set_text(MenuEntry *e, const char *t) { e->fg = (uintptr_t)t; }
static void set_back(MenuEntry *e, uint16_t c)
{
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = c;
}
static void set_color(MenuEntry *e, uint16_t c) { e->stringFlags = (uint16_t)((e->stringFlags & ~0x0F) | c); }
static int32_t pages(int32_t n) { return n ? (n + kRows - 1) / kRows : 1; }

/* SWOS 2020's shortenStr: cut to fit `limit` pixels, "..." at the end */
static void shorten_to(char *s, const MenuEntry *e, int32_t limit)
{
    extern void *_charTables[];
    uint8_t *table = (uint8_t *)_charTables[((e->stringFlags & STR_FLAGS_MASK) >> 2) / 4];
    size_t n = strlen(s);
    if (GetTextWidth((uint8_t *)s, table) <= limit)
        return;
    while (n > 0) {
        s[n] = 0;
        char tmp[80];
        snprintf(tmp, sizeof tmp, "%s...", s);
        if (GetTextWidth((uint8_t *)tmp, table) <= limit || n == 1) {
            snprintf(s + n, 4, "...");
            return;
        }
        n--;
    }
}

static void show(const char *name)
{
    uint8_t *menu = MenuJson_Load(name);
    if (menu)
        ShowMenu(menu);
    else
        fprintf(stderr, "[galore2020] cannot show %s\n", name);
}

/* A download waited for, frames presented meanwhile: the window answers (a plain sleep left it
 * "not responding" on a slow line). The service's answer, 1 when it never came. */
static int32_t download(const char *method, const char *args, char *out, uint32_t cap)
{
    const uint32_t start = SDL_GetTicks();
    int32_t rc = 1;
    while (rc == 1 && SDL_GetTicks() - start < 30000) {
        rc = ModApi_CallService("online-records", method, args, out, cap);
        if (rc == 1 && Platform_VBlank())       /* (the window closed) */
            break;
    }
    return rc;
}

/* A message with one button. The fire that led here may still be down: not its answer. */
static void tell(const char *title, const char *text)
{
    static const char *const kOk[1] = { "OK" };
    _fireResetFlag = 1;
    ModApi_ShowDialog(title, text, kOk, 1, 0);
}

/* ---- SWOS GALORE ---- */

static int32_t s_monthly, s_monthIndex;     /* the monthly chart: 0 this month, 11 a year ago */

static void swosGaloreMenu_OnDraw(void)
{
    const int32_t sel = selected_ordinal();
    set_text(entry_at(SwosGaloreMenu_desc),
             sel == SwosGaloreMenu_allTimeChart ? "THE BEST GOALS EVER SHARED"
             : sel == SwosGaloreMenu_monthlyChart ? "THE BEST GOALS OF A MONTH"
             : sel == SwosGaloreMenu_latestUploads ? "CHECK OUT THE LATEST UPLOADS AND RATE THEM"
             : sel == SwosGaloreMenu_shareHighlights ? "SHARE YOUR OWN HIGHLIGHTS ON SWOS GALORE" : "");
}
static void selectAllTimeChart(void)
{
    s_monthly = 0;
    show("swos2020/chartHighlightsMenu");
}
static void selectMonthlyChart(void)
{
    s_monthly = 1;
    s_monthIndex = 0;
    show("swos2020/chartHighlightsMenu");
}

/* ---- the chart ---- */

typedef struct { char user[32], shown[32], file[96], comment[64], date[12]; int32_t rank, scene, raters; double rate; } Row;
/* (user: as the server spells it -- its folders are case-sensitive; shown: in capitals) */
static Row    *s_rows;
static int32_t s_numRows, s_state, s_page, s_fresh;
static char    s_chart[1 << 18], s_title[48], s_month[40], s_monthArg[16], s_pageText[24], s_desc[96], s_hint[64];
static char    s_cells[kRows][4][64];

static void month_of(int32_t back, int32_t *year, int32_t *month)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    int32_t y = t ? t->tm_year + 1900 : 2026, m = t ? t->tm_mon + 1 : 1;
    m -= back;
    while (m < 1) {
        m += 12;
        y--;
    }
    *year = y;
    *month = m;
}

static void parse(void)
{
    if (!s_rows && (s_rows = (Row *)malloc(sizeof(Row) * kMax)) == NULL)
        return;
    s_numRows = 0;
    for (char *line = s_chart; line && *line && s_numRows < kMax;) {
        char *next = strchr(line, '\n');
        Row *r = &s_rows[s_numRows];
        char *f[8];
        int32_t n = 0;
        if (next)
            *next = 0;
        for (char *p = line; p && n < 8; n++) {
            f[n] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = 0;
        }
        if (n == 8) {
            r->rank = atoi(f[0]);
            snprintf(r->user, sizeof r->user, "%s", f[1]);
            snprintf(r->shown, sizeof r->shown, "%s", f[1]);
            snprintf(r->file, sizeof r->file, "%s", f[2]);
            /* (no comment: SWOS 2020 stored "%0" for it, and showed the file's name) */
            snprintf(r->comment, sizeof r->comment, "%s", strcmp(f[3], "%0") ? f[3] : "");
            snprintf(r->date, sizeof r->date, "%s", f[4]);
            r->scene = atoi(f[5]);
            r->rate = atof(f[6]);
            r->raters = atoi(f[7]);
            for (char *c = r->shown; *c; c++)
                if (*c >= 'a' && *c <= 'z')
                    *c = (char)(*c - 32);
            s_numRows++;
        }
        line = next ? next + 1 : NULL;
    }
}

static void rate_AfterDraw(MenuEntry *e)
{
    const int32_t i = (int32_t)e->ordinal - ChartHighlightsMenu_avgRate_01, idx = s_page * kRows + i;
    if (i < 0 || i >= kRows || idx >= s_numRows || s_rows[idx].rate <= 0)
        return;
    United_DrawRating((int32_t)(s_rows[idx].rate * 2 + 0.5), e->x + 1, e->y + 3);
}

static void arrow_AfterDraw(MenuEntry *e)
{
    United_DrawPageArrow(e->ordinal == ChartHighlightsMenu_scrollDown, e->x + 2, e->y + 1);
}

static void nav4(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down);

/* The keys and the pad (the imported menu links nothing but EXIT: SWOS 2020 linked the rest as it
 * drew, so only the mouse reached a highlight): the comments are the list, the month's arrows above
 * it (MONTHLY), the page arrows to its right, NEWS ON DISCORD and EXIT below. */
static void chart_nav(void)
{
    int32_t n = s_numRows - s_page * kRows;
    n = n > kRows ? kRows : n < 0 ? 0 : n;
    const int32_t first = ChartHighlightsMenu_filename_01, last = n ? first + n - 1 : -1;
    const int32_t top = s_monthly ? ChartHighlightsMenu_yearMonth_Dec : -1;
    const int32_t under = n ? first : ChartHighlightsMenu_menuExit;
    for (int32_t i = 0; i < n; i++)
        nav4(first + i, -1, i < 3 ? ChartHighlightsMenu_scrollUp : ChartHighlightsMenu_scrollDown,
             i ? first + i - 1 : top, i + 1 < n ? first + i + 1 : ChartHighlightsMenu_menuExit);
    if (s_monthly) {
        nav4(ChartHighlightsMenu_yearMonth_Dec, -1, ChartHighlightsMenu_yearMonth_Inc, -1, under);
        nav4(ChartHighlightsMenu_yearMonth_Inc, ChartHighlightsMenu_yearMonth_Dec, -1, -1, under);
    }
    nav4(ChartHighlightsMenu_scrollUp, n ? first : -1, -1, top, ChartHighlightsMenu_scrollDown);
    nav4(ChartHighlightsMenu_scrollDown, last, -1, ChartHighlightsMenu_scrollUp, ChartHighlightsMenu_menuExit);
    nav4(ChartHighlightsMenu_menuExit, ChartHighlightsMenu_newsOnDiscord, -1, n ? last : top, -1);
    nav4(ChartHighlightsMenu_newsOnDiscord, -1, ChartHighlightsMenu_menuExit, n ? last : top, -1);
}

static void ChartHighlightsMenu_OnInit(void)
{
    s_state = 1;
    s_fresh = 1;            /* (asked again each time: ratings and shares since) */
    s_page = 0;
    s_desc[0] = 0;
}
static void ChartHighlightsMenu_OnReturn(void) {}

static void ChartHighlightsMenu_OnDraw(void)
{
    static const char *const heads[5] = { "RANK", "DATE", "PLAYER", "COMMENT", "RATE" };
    static const int32_t col0[5] = { ChartHighlightsMenu_ranking_00, ChartHighlightsMenu_datetime_00,
                                     ChartHighlightsMenu_username_00, ChartHighlightsMenu_filename_00,
                                     ChartHighlightsMenu_avgRate_00 };
    if (s_state == 1) {
        char args[64];
        if (s_monthly) {
            int32_t y, m;
            month_of(s_monthIndex, &y, &m);
            snprintf(s_monthArg, sizeof s_monthArg, "%04d-%02d", (int)y, (int)m);
        } else {
            s_monthArg[0] = 0;
        }
        snprintf(args, sizeof args, "{\"month\":\"%s\"%s}", s_monthArg, s_fresh ? ",\"fresh\":true" : "");
        s_fresh = 0;
        s_state = ModApi_CallService("online-records", "galore.chart", args, s_chart, sizeof s_chart);
        if (s_state == 0)
            parse();
        else
            s_numRows = 0;
    }
    snprintf(s_title, sizeof s_title, "CHART HIGHLIGHTS (%d)", (int)s_numRows);
    set_text(entry_at(ChartHighlightsMenu_header), s_title);
    if (s_monthly) {
        static const char *const months[12] = { "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE", "JULY",
                                                "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER" };
        int32_t y, m;
        month_of(s_monthIndex, &y, &m);
        snprintf(s_month, sizeof s_month, "%s %d", months[m - 1], (int)y);
    } else {
        snprintf(s_month, sizeof s_month, "ALL TIME CHART");
    }
    set_text(entry_at(ChartHighlightsMenu_yearMonth), s_month);
    entry_at(ChartHighlightsMenu_yearMonth_Dec)->isInvisible = !s_monthly;
    entry_at(ChartHighlightsMenu_yearMonth_Inc)->isInvisible = !s_monthly;
    for (int32_t c = 0; c < 5; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }
    const int32_t sel = selected_ordinal();
    int32_t selRow = -1;
    for (int32_t c = 0; c < 5; c++)
        if (sel >= col0[c] + 1 && sel <= col0[c] + kRows)
            selRow = sel - col0[c] - 1;
    const int32_t numPages = pages(s_numRows);
    if (s_page >= numPages)
        s_page = numPages - 1;
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t idx = s_page * kRows + i;
        const Row *r = idx < s_numRows ? &s_rows[idx] : NULL;
        if (r) {
            snprintf(s_cells[i][0], 64, "%d", (int)r->rank);
            snprintf(s_cells[i][1], 64, "%s", r->date);
            snprintf(s_cells[i][2], 64, "%s", r->shown);
            snprintf(s_cells[i][3], 64, "%s", r->comment[0] ? r->comment : r->file);
            for (int32_t c = 2; c < 4; c++) {
                const MenuEntry *e = entry_at(col0[c] + 1 + i);
                shorten_to(s_cells[i][c], e, (int32_t)e->dwWidth - 6);
            }
        } else {
            s_cells[i][0][0] = s_cells[i][1][0] = s_cells[i][2][0] = s_cells[i][3][0] = 0;
        }
        for (int32_t c = 0; c < 4; c++) {
            set_text(entry_at(col0[c] + 1 + i), s_cells[i][c]);
            set_back(entry_at(col0[c] + 1 + i), c == 0 ? kPurple : i == selRow ? kRed : kGray);
        }
        set_text(entry_at(ChartHighlightsMenu_avgRate_01 + i), "");
        set_back(entry_at(ChartHighlightsMenu_avgRate_01 + i), i == selRow ? kRed : kGray);
        entry_at(ChartHighlightsMenu_avgRate_01 + i)->afterDrawCb = rate_AfterDraw;
    }
    snprintf(s_pageText, sizeof s_pageText, "(%d / %d)", (int)s_page + 1, (int)numPages);
    set_text(entry_at(ChartHighlightsMenu_page), s_pageText);
    entry_at(ChartHighlightsMenu_scrollUp)->afterDrawCb = arrow_AfterDraw;
    entry_at(ChartHighlightsMenu_scrollDown)->afterDrawCb = arrow_AfterDraw;
    chart_nav();
    /* the picked one's comment in full, and what is happening (SWOS 2020's viewHilData) */
    {
        const int32_t idx = s_page * kRows + selRow;
        if (s_state == 1)
            snprintf(s_hint, sizeof s_hint, "LOADING...");
        else if (s_state < 0)
            snprintf(s_hint, sizeof s_hint, "SENSIBLESOCCER.DE CANNOT BE REACHED");
        else if (!s_numRows)
            snprintf(s_hint, sizeof s_hint, "NO HIGHLIGHTS IN THIS CHART");
        else
            snprintf(s_hint, sizeof s_hint, "%s",
                     selRow >= 0 && idx < s_numRows ? "FIRE ON A HIGHLIGHT WATCHES IT"
                     : sel == ChartHighlightsMenu_menuExit ? "BACK TO THE PREVIOUS MENU"
                     : sel == ChartHighlightsMenu_newsOnDiscord ? "VIEW NEWS OF NEW SHARED HIGHLIGHTS ON DISCORD"
                     : sel == ChartHighlightsMenu_yearMonth_Dec || sel == ChartHighlightsMenu_yearMonth_Inc
                       ? "CHANGE THE SEARCH PERIOD OF CHARTS" : "");
        set_text(entry_at(ChartHighlightsMenu_menuHint), s_hint);
        if (selRow >= 0 && idx < s_numRows)
            snprintf(s_desc, sizeof s_desc, "%s (%.2f, %d RATER%s)",
                     s_rows[idx].comment[0] ? s_rows[idx].comment : s_rows[idx].file, s_rows[idx].rate,
                     (int)s_rows[idx].raters, s_rows[idx].raters == 1 ? "" : "S");
        else
            s_desc[0] = 0;
        set_text(entry_at(ChartHighlightsMenu_longDesc), s_desc);
        set_color(entry_at(ChartHighlightsMenu_longDesc), kYellowText);
    }
}

static void drawStarIconsForChartHighlights(void) {}     /* (the rows' own afterDraws draw them) */

/* the highlight: downloaded (SWOS 2020 waited too), then its scene in the viewer */
static void ChartHighlights_Filename_onSelect(void)
{
    const int32_t sel = selected_ordinal();
    const int32_t row = sel - ChartHighlightsMenu_filename_01, idx = s_page * kRows + row;
    if (row < 0 || row >= kRows || idx >= s_numRows)
        return;
    char args[256], u[64], f[160], out[32];
    snprintf(args, sizeof args, "{\"user\":\"%s\",\"file\":\"%s\"}", ModApi_JsonEscape(s_rows[idx].user, u, sizeof u),
             ModApi_JsonEscape(s_rows[idx].file, f, sizeof f));
    const int32_t rc = download("galore.download", args, out, sizeof out);
    /* (said in a box: the line under the table is the picked row's comment, drawn over any other) */
    if (rc != 0)
        tell("VIEW HIGHLIGHT", "THE HIGHLIGHT COULD NOT BE DOWNLOADED");
    else if (ModApi_ViewHighlights("galore", "view.HIL", s_rows[idx].scene) != 0)
        tell("VIEW HIGHLIGHT", "HIGHLIGHT FILE IS NOT AVAILABLE");     /* (not one this game reads) */
    _fireResetFlag = 1;
}

static void ChartHighlights_ScrollUp_onSelect(void) { s_page = (s_page + pages(s_numRows) - 1) % pages(s_numRows); }
static void ChartHighlights_ScrollDown_onSelect(void) { s_page = (s_page + 1) % pages(s_numRows); }
static void ChartHighlights_menuExit_onSelect(void) { SetExitMenuFlag(); }
static void yearMonth_Dec_onSelect(void)          /* (SWOS 2020's: DEC goes back a month, round the twelve) */
{
    s_monthIndex = (s_monthIndex + 1) % 12;
    s_state = 1;
    s_page = 0;
}
static void yearMonth_Inc_onSelect(void)
{
    s_monthIndex = (s_monthIndex + 11) % 12;
    s_state = 1;
    s_page = 0;
}

/* ======================================================================================
 * RATE HIGHLIGHTS and SHARE HIGHLIGHTS (SWOS 2020's replays/replaysMenu.cpp: RateHighlight,
 * ShareHighlight), behind its SERVER LOGIN (OnlineMenus_ServerLogIn), and VIEW HIGHLIGHT, the box
 * before a highlight is shown (viewHighlightMenu: its game style, its scene(s)).
 *
 *   RATE   every shared highlight, newest first, seven a page: date, player, comment, the player's
 *          own rating -- CLICK, then 3.0, 3.5 .. 5.0 and off again (not your own highlights); the
 *          comment's button views the highlight. The ratings are sent on EXIT (only what changed).
 *   SHARE  the .HIL files in the save folder: comment (typed, kept beside: the save folder's
 *          mods/galore2020/<NAME>.TXT -- the comment, then the scene, as SWOS 2020's
 *          highlights\<NAME>.TXT), SHARE ON/OFF, the SCENE promoted (ALL, 1..n). On EXIT (a shared
 *          one needs a comment; LATEST.HIL is never shared): highlights no longer shared deleted,
 *          new ones sent with their file, changed ones' rows updated -- and SWOS 2020's Discord
 *          post of what was shared (PROJECT_DISCORD_PROXY, "galore_share").
 * The network work runs on a thread of its own (s_job); the screen shows how far it is.
 * ====================================================================================== */
#if defined(SWOS_HTTPS)
#include "online_menus.h"
#include "project_identity.h"
#include "united_api.h"
#include "united_records.h"
#include "https_client.h"
#include "input_text.h"
#include "platform_sdl.h"
#include "swos_io.h"
#endif

enum { kDarkBlue = 3, kLightBlue = 13, kGreenBack = 14 };

typedef struct {
    char    user[40], file[96], note[64], datetime[24];
    int32_t scene, maxScenes, shared, style;
    double  rate;
    /* as the server had it (what EXIT compares with) */
    double  rateWas;
    int32_t sharedWas, sceneWas;
    char    noteWas[64];
    int32_t away;       /* SHARE: shared on the site, no file of it here */
} Hil;

static Hil    *s_hil;               /* RATE: every shared highlight; SHARE: the local files */
static int32_t s_hilN, s_hilPage, s_hilMode;    /* mode: 1 RATE, 2 SHARE */
static char    s_hilTitle[48], s_hilPageText[24], s_hilDesc[96], s_hilHint[80];
static char    s_hilCells[kRows][4][64];
static int32_t s_sendFailed;        /* RATE / SHARE: something could not be sent on EXIT */
static int32_t s_shareUnknown;      /* SHARE: the server's list of the player's own did not come */

static char    s_me[40];            /* the logged-in player, as the site has them */

static void open_url_discord(void);
static void view_server(const Hil *h);

static void upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

static void nav4(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
}

/* SWOS 2020's table moves (viewHilData / viewLocalHilData): four columns of seven, left and right
 * along a row, up and down a column (the last row's down: EXIT), the right column's right the page
 * arrows (rows 1-3 up, 4-7 down); EXIT up to the third column's last row, left to NEWS ON DISCORD. */
static void table_nav(const int32_t col0[4], int32_t scrollUp, int32_t scrollDown, int32_t exitEntry,
                      int32_t newsEntry)
{
    for (int32_t c = 0; c < 4; c++)
        for (int32_t i = 0; i < kRows; i++) {
            const int32_t o = col0[c] + 1 + i;
            nav4(o, c > 0 ? col0[c - 1] + 1 + i : -1, c < 3 ? col0[c + 1] + 1 + i : i < 3 ? scrollUp : scrollDown,
                 i > 0 ? o - 1 : -1, i < kRows - 1 ? o + 1 : exitEntry);
        }
    nav4(scrollUp, col0[3] + 1, -1, -1, scrollDown);
    nav4(scrollDown, col0[3] + kRows, -1, scrollUp, -1);
    nav4(exitEntry, newsEntry, -1, col0[2] + kRows, -1);
    nav4(newsEntry, -1, exitEntry, col0[0] + kRows, -1);
}

/* A highlights file's scenes and game style (SWOS 2020's getMaxScenes, getGameStyleFromFile): the
 * PC's / SWOS 2020's (num_shots at 3616, the style at 5), SWOS's own (num_goals at 2244) or ours
 * (JSON "num_goals"; the style by its "replay_fps" -- 60 as AMIGA 60 FPS, XBOX's rate too).
 * style -1: not in the file (the viewer's own rate). */
static int32_t hil_scenes(const uint8_t *d, uint32_t n, int32_t *style)
{
    extern int DosReplay_IsDosHil(const uint8_t *buf, uint32_t size);
    extern uint32_t _kHilFileSig;
    *style = -1;
    if (!d || !n)
        return 0;
    if (DosReplay_IsDosHil(d, n)) {
        const uint8_t b = n > 5 ? d[5] : 0;
        *style = b == 0xF4 ? -1 : b >= 30 ? b - 30 : b >= 20 ? b - 20 : b >= 10 ? b - 10 : -1;
        if (*style == 2)
            *style = 1;
        if (*style > 4)         /* (not a style: VIEW HIGHLIGHT names the styles by it) */
            *style = -1;
        return n >= 3618 ? (int32_t)(int16_t)(d[3616] | (d[3617] << 8)) : 0;
    }
    if (d[0] == '{') {
        const char *k = strstr((const char *)d, "\"num_goals\"");
        const char *c = k ? strchr(k, ':') : NULL;
        const char *f = strstr((const char *)d, "\"replay_fps\"");      /* (the rate it was played at) */
        const char *v = f ? strchr(f, ':') : NULL;
        const int32_t fps = v ? atoi(v + 1) : 0;
        *style = fps == 70 ? 0 : fps == 60 ? 3 : fps == 50 ? 1 : -1;
        return c ? atoi(c + 1) : 0;
    }
    if (n >= 2246 && BE_READ_U32(d) == _kHilFileSig)
        return (int32_t)BE_READ_U16(d + 2244);
    return 0;
}

/* ---- the work on its thread ---- */

enum { JOB_NONE, JOB_RATE_LOAD, JOB_RATE_SEND, JOB_SHARE_LOAD, JOB_SHARE_SEND };
static int32_t           s_jobKind;
static volatile int32_t  s_jobState;        /* 0 idle, 1 running, 2 done, 3 failed */
static volatile int32_t  s_jobDone, s_jobTotal, s_jobPhase;   /* phase: 0 deleting, 1 updating, 2 uploading */
static char             *s_jobText;         /* RATE_LOAD / SHARE_LOAD: what came back */
static char             *s_jobText2;        /* RATE_LOAD: the player's ratings (what EXIT compares with) */
enum { kJobText = 1 << 20, kDiscordCards = 10 };

#if defined(SWOS_HTTPS)
static void style_name_of(int32_t style, char *out, uint32_t cap)
{
    static const char *const k[5] = { "DOS", "AMIGA", "MOON", "60 FPS", "XBOX" };
    snprintf(out, cap, "%s", style >= 0 && style < 5 ? k[style] : "AMIGA");
}

static void b64url(const char *in, char *out, uint32_t cap)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    const uint8_t *d = (const uint8_t *)in;
    size_t n = strlen(in), i = 0;
    uint32_t o = 0;
    for (; i + 3 <= n && o + 5 < cap; i += 3) {
        const uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8 | d[i + 2];
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = t[v >> 6 & 63];
        out[o++] = t[v & 63];
    }
    if (n - i == 1 && o + 3 < cap) {
        const uint32_t v = (uint32_t)d[i] << 16;
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
    } else if (n - i == 2 && o + 4 < cap) {
        const uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)d[i + 1] << 8;
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = t[v >> 6 & 63];
    }
    out[o] = 0;
}

static void json_esc(const char *in, char *out, uint32_t cap)
{
    uint32_t o = 0;
    for (; *in && o + 3 < cap; in++) {
        if (*in == '"' || *in == '\\')
            out[o++] = '\\';
        if ((unsigned char)*in >= ' ')
            out[o++] = *in;
    }
    out[o] = 0;
}

/* SWOS 2020's postGaloreShareToDiscord: the announcement and a card a highlight (kDiscordCards at
 * most: the relay refuses a post with more) */
static void discord_post(const char *cards, int32_t count)
{
    char announce[160], *body, *ea, *ec;
    HttpsReply r;
    if (!PROJECT_DISCORD_PROXY[0] || !count || getenv("SWOS_GALORE_NO_DISCORD"))
        return;
    snprintf(announce, sizeof announce, ":man_raising_hand: `%s` has shared %s :rotating_light:", s_me,
             count == 1 ? "a new HIGHLIGHT" : "new HIGHLIGHTS");
    ea = (char *)malloc(strlen(announce) * 3 + 1);
    ec = (char *)malloc(strlen(cards) * 3 + 1);
    body = ea && ec ? (char *)malloc(strlen(announce) * 3 + strlen(cards) * 3 + 64) : NULL;
    if (body) {
        Https_Escape(announce, ea, (uint32_t)strlen(announce) * 3 + 1);
        Https_Escape(cards, ec, (uint32_t)strlen(cards) * 3 + 1);
        sprintf(body, "type=galore_share&announce=%s&cards=%s", ea, ec);
        memset(&r, 0, sizeof r);
        Https_Request(PROJECT_DISCORD_PROXY, body, &r);
        fprintf(stderr, "[galore2020] the share %s to Discord (%d highlight%s): %d\n",
                r.status >= 200 && r.status < 300 ? "posted" : "NOT posted", (int)count, count == 1 ? "" : "s",
                (int)r.status);
        Https_Free(&r);
    }
    free(body);
    free(ea);
    free(ec);
}

static int job_main(void *unused)
{
    (void)unused;
    int32_t ok = 1;
    if (s_jobKind == JOB_RATE_LOAD) {
        ok = UnitedRecords_GaloreRatings(s_me, s_jobText2, kJobText) && UnitedRecords_GaloreAll(s_jobText, kJobText);
    } else if (s_jobKind == JOB_SHARE_LOAD) {
        ok = UnitedRecords_GaloreShared(s_me, s_jobText, kJobText);
    } else if (s_jobKind == JOB_RATE_SEND) {
        /* (SWOS 2020's updateServerData: a rating taken away -- update_avg_rate_data --, one new or
         *  changed -- upsert_rater_data. Not its third part: the ratings of highlights no longer
         *  shared dropped one request each -- they count nowhere (a chart is the shared highlights'),
         *  and a player's years of them made the first EXIT take minutes; Playa, 2026-11-21) */
        s_jobTotal = s_hilN;
        for (int32_t i = 0; i < s_hilN; i++) {
            const Hil *h = &s_hil[i];
            s_jobDone = i + 1;
            if (h->rate != h->rateWas && (h->rate > 0.0 || h->rateWas > 0.0))
                ok &= UnitedRecords_GaloreRate(s_me, h->user, h->file, h->rate);
        }
    } else if (s_jobKind == JOB_SHARE_SEND) {
        char *cards = (char *)malloc(65536);
        uint32_t used = 0;
        int32_t count = 0;
        if (cards)
            cards[used++] = '[';
        s_jobPhase = 0;
        s_jobTotal = s_hilN;
        for (int32_t i = 0; i < s_hilN; i++) {      /* (no longer shared: its row deleted) */
            s_jobDone = i + 1;
            if (s_hil[i].sharedWas && !s_hil[i].shared)
                ok &= UnitedRecords_GaloreUnshare(s_me, s_hil[i].file);
        }
        s_jobPhase = 1;
        for (int32_t i = 0; i < s_hilN; i++) {
            Hil *h = &s_hil[i];
            s_jobDone = i + 1;
            if (!h->shared || (h->sharedWas && !strcmp(h->note, h->noteWas) && h->scene == h->sceneWas))
                continue;
            uint8_t *data = NULL;
            uint32_t size = 0;
            if (!h->sharedWas) {                    /* (new: its file goes with it) */
                char path[1024];
                s_jobPhase = 2;
                snprintf(path, sizeof path, "%s/%s", swos_save_dir(), h->file);
                data = (uint8_t *)ModApi_ReadFile(path, &size);
                if (!data || !size) {               /* (no row without its file) */
                    free(data);
                    s_jobPhase = 1;
                    ok = 0;
                    continue;
                }
            }
            const int32_t sent = UnitedRecords_GaloreShare(s_me, h->file, h->note, h->datetime, h->scene, data, size);
            free(data);
            /* a new one whose file did not arrive: its row taken back, or it would count as shared
             * (listed, nothing to download) and never be sent again. Only when the server said
             * which were shared: this one was not */
            if (!sent && !h->sharedWas && !s_shareUnknown)
                UnitedRecords_GaloreUnshare(s_me, h->file);
            s_jobPhase = 1;
            ok &= sent;
            if (sent && cards && used + 1024 < 65536) {     /* (the Discord card) */
                char key[160], link[256], title[200], note[160], style[16], scene[32];
                snprintf(key, sizeof key, "%s/%s", s_me, h->file);
                if (h->scene > 0)
                    snprintf(key + strlen(key), sizeof key - strlen(key), ":%d", (int)h->scene);
                b64url(key, link, sizeof link);
                json_esc(h->file, title, sizeof title);
                json_esc(h->note, note, sizeof note);
                style_name_of(h->style, style, sizeof style);
                if (h->scene == 0)
                    snprintf(scene, sizeof scene, "All %d scene%s", (int)h->maxScenes, h->maxScenes == 1 ? "" : "s");
                else
                    snprintf(scene, sizeof scene, "%d of %d", (int)h->scene, (int)h->maxScenes);
                used += (uint32_t)snprintf(cards + used, 65536 - used,
                                           "%s{\"title\":\"%s\",\"url\":\"https://swos.eu/rpl/?g=%s\",\"style\":\"%s\","
                                           "\"scene\":\"%s\",\"comment\":\"%s\"}",
                                           count ? "," : "", title, link, style, scene, note);
                if (++count == kDiscordCards) {     /* (full: this post sent, the next begun) */
                    snprintf(cards + used, 65536 - used, "]");
                    discord_post(cards, count);
                    used = 1;
                    count = 0;
                }
            }
        }
        if (cards) {
            snprintf(cards + used, 65536 - used, "]");
            discord_post(cards, count);
            free(cards);
        }
    }
    s_jobState = ok ? 2 : 3;
    return 0;
}
#endif

/* Start a job (0), or not (a build without HTTPS, or one running). */
static int32_t job_start(int32_t kind)
{
#if defined(SWOS_HTTPS)
    SDL_Thread *t;
    if (s_jobState == 1)
        return -1;
    if (!s_jobText)
        s_jobText = (char *)malloc(kJobText);
    if (!s_jobText2)
        s_jobText2 = (char *)malloc(kJobText);
    if (!s_jobText || !s_jobText2)
        return -1;
    if (kind == JOB_RATE_LOAD || kind == JOB_SHARE_LOAD) {
        s_jobText[0] = 0;
        s_jobText2[0] = 0;
    }
    s_jobKind = kind;
    s_jobDone = s_jobTotal = s_jobPhase = 0;
    s_jobState = 1;
    t = SDL_CreateThread(job_main, "galore", NULL);
    if (!t) {
        s_jobState = 3;
        return -1;
    }
    SDL_DetachThread(t);
    return 0;
#else
    (void)kind;
    return -1;
#endif
}

static int32_t job_percent(void) { return s_jobTotal ? (int32_t)(s_jobDone * 100 / s_jobTotal) : 100; }

static int32_t hil_alloc(void)
{
    if (!s_hil)
        s_hil = (Hil *)malloc(sizeof(Hil) * 2000);
    return s_hil != NULL;
}

/* ---- RATE HIGHLIGHTS ---- */

static void rate_parse(void)
{
    s_hilN = 0;
    if (!hil_alloc())
        return;
    for (char *line = s_jobText; line && *line && s_hilN < 2000;) {
        char *next = strchr(line, '\n'), *f[5];
        int32_t n = 0;
        if (next)
            *next++ = 0;
        for (char *p = line; p && n < 5; n++) {
            f[n] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = 0;
        }
        if (n == 5 && f[0][0]) {
            Hil *h = &s_hil[s_hilN++];
            memset(h, 0, sizeof *h);
            snprintf(h->user, sizeof h->user, "%s", f[0]);
            snprintf(h->file, sizeof h->file, "%s", f[1]);
            snprintf(h->note, sizeof h->note, "%s", f[2]);
            snprintf(h->datetime, sizeof h->datetime, "%s", f[3]);
            h->scene = atoi(f[4]);
            /* the player's own rating of it (downloadServerHilData) */
            char want[160];
            snprintf(want, sizeof want, "%s\t%s\t", h->user, h->file);
            const char *r = strstr(s_jobText2, want);
            while (r && r != s_jobText2 && r[-1] != '\n')
                r = strstr(r + 1, want);
            h->rate = h->rateWas = r ? atof(r + strlen(want)) : 0.0;
        }
        line = next;
    }
}

static void rateStars_AfterDraw(MenuEntry *e)
{
    const int32_t i = (int32_t)e->ordinal - RateHighlightsMenu_rate_01, idx = s_hilPage * kRows + i;
    if (i < 0 || i >= kRows || idx >= s_hilN || s_hil[idx].rate <= 0)
        return;
    United_DrawRating((int32_t)(s_hil[idx].rate * 2 + 0.5), e->x + 1, e->y + 3);
}

static void hilArrow_AfterDraw(MenuEntry *e)
{
    const int32_t down = s_hilMode == 1 ? e->ordinal == RateHighlightsMenu_scrollDown
                                        : e->ordinal == ShareHighlightsMenu_scrollDown;
    United_DrawPageArrow(down, e->x + 2, e->y + 1);
}

static void RateHighlightsMenu_OnInit(void)
{
    s_hilMode = 1;
    s_hilN = 0;
    s_hilPage = 0;
    s_hilDesc[0] = 0;
    job_start(JOB_RATE_LOAD);
}
static void RateHighlightsMenu_OnReturn(void) {}

static void RateHighlightsMenu_OnDraw(void)
{
    static const char *const heads[4] = { "DATE", "PLAYER", "                COMMENT", "RATE" };
    static const int32_t col0[4] = { RateHighlightsMenu_datetime_00, RateHighlightsMenu_username_00,
                                     RateHighlightsMenu_filename_00, RateHighlightsMenu_rate_00 };
    if (s_jobKind == JOB_RATE_LOAD && s_jobState == 2) {
        rate_parse();
        s_jobState = 0;
    }
    for (int32_t c = 0; c < 4; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }
    snprintf(s_hilTitle, sizeof s_hilTitle, "RATE HIGHLIGHTS (%d)", (int)s_hilN);
    set_text(entry_at(RateHighlightsMenu_header), s_hilTitle);
    if (s_hilPage >= pages(s_hilN))
        s_hilPage = 0;
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t idx = s_hilPage * kRows + i;
        const Hil *h = idx < s_hilN ? &s_hil[idx] : NULL;
        for (int32_t c = 0; c < 4; c++)
            s_hilCells[i][c][0] = 0;
        if (h) {
            const char *d = h->datetime;        /* 2026-11-21-... -> 26.11.21 */
            if (strlen(d) >= 10)
                snprintf(s_hilCells[i][0], 64, "%.2s.%.2s.%.2s", d + 2, d + 5, d + 8);
            snprintf(s_hilCells[i][1], 64, "%s", h->user);
            upper(s_hilCells[i][1]);
            shorten_to(s_hilCells[i][1], entry_at(RateHighlightsMenu_username_01 + i), RateHighlightsMenu_kRH_UsernameWidth - 8);
            snprintf(s_hilCells[i][2], 64, "%s", h->note[0] ? h->note : h->file);
            shorten_to(s_hilCells[i][2], entry_at(RateHighlightsMenu_filename_01 + i), RateHighlightsMenu_kRH_FilenameWidth - 8);
            snprintf(s_hilCells[i][3], 64, "%s", h->rate == 0.0 ? "CLICK" : "");
        }
        for (int32_t c = 0; c < 4; c++)
            set_text(entry_at(col0[c] + 1 + i), s_hilCells[i][c]);
        set_back(entry_at(RateHighlightsMenu_rate_01 + i), s_hilCells[i][3][0] ? kLightBlue : kDarkBlue);
        entry_at(RateHighlightsMenu_rate_01 + i)->afterDrawCb = rateStars_AfterDraw;
    }
    snprintf(s_hilPageText, sizeof s_hilPageText, "(%d OF %d)", (int)s_hilPage + 1, (int)pages(s_hilN));
    set_text(entry_at(RateHighlightsMenu_page), s_hilPageText);
    entry_at(RateHighlightsMenu_scrollUp)->afterDrawCb = hilArrow_AfterDraw;
    entry_at(RateHighlightsMenu_scrollDown)->afterDrawCb = hilArrow_AfterDraw;
    entry_at(RateHighlightsMenu_dummy)->isInvisible = 1;
    table_nav(col0, RateHighlightsMenu_scrollUp, RateHighlightsMenu_scrollDown, RateHighlightsMenu_menuExit,
              RateHighlightsMenu_newsOnDiscord);
    /* the line under the table: the picked cell in full (viewHilData) -- or how the work goes */
    {
        const int32_t m = selected_ordinal();
        int32_t row = -1, col = -1;
        for (int32_t c = 0; c < 4; c++)
            if (m >= col0[c] + 1 && m <= col0[c] + kRows) {
                row = m - col0[c] - 1;
                col = c;
            }
        const int32_t idx = s_hilPage * kRows + row;
        const Hil *h = row >= 0 && idx < s_hilN ? &s_hil[idx] : NULL;
        s_hilDesc[0] = 0;
        if (s_jobState == 1 && s_jobKind == JOB_RATE_LOAD)
            snprintf(s_hilDesc, sizeof s_hilDesc, "LOADING...");
        else if (s_jobState == 1 && s_jobKind == JOB_RATE_SEND)
            snprintf(s_hilDesc, sizeof s_hilDesc, "UPDATING SERVER DATA (%d%%)...", (int)job_percent());
        else if (s_jobState == 3 && s_jobKind == JOB_RATE_LOAD)
            snprintf(s_hilDesc, sizeof s_hilDesc, "SENSIBLESOCCER.DE CANNOT BE REACHED");
        else if (h && col == 0)
            snprintf(s_hilDesc, sizeof s_hilDesc, "%s", h->datetime);
        else if (h && col == 1)
            snprintf(s_hilDesc, sizeof s_hilDesc, "%s", h->user);
        else if (h && col == 2)     /* (a cut comment and the file in turn, eight seconds each) */
            snprintf(s_hilDesc, sizeof s_hilDesc, "%s",
                     h->note[0] && strstr(s_hilCells[row][2], "...") && (SDL_GetTicks() / 8000) % 2 == 0 ? h->note
                                                                                                        : h->file);
        else if (h && col == 3 && h->rate != 0.0)
            snprintf(s_hilDesc, sizeof s_hilDesc, "RATING: %3.1f", h->rate);
        else if (h && col == 3)
            snprintf(s_hilDesc, sizeof s_hilDesc, "NOT RATED");
        upper(s_hilDesc);
        set_text(entry_at(RateHighlightsMenu_longDesc), s_hilDesc);
        snprintf(s_hilHint, sizeof s_hilHint, "%s",
                 m == RateHighlightsMenu_menuExit ? "RATINGS ARE SUBMITTED UPON EXIT"
                 : col == 2 ? "HIT FIRE TO DOWNLOAD AND VIEW THE HIGHLIGHT"
                 : col == 3 ? "HIT FIRE TO TOGGLE RATING"
                 : m == RateHighlightsMenu_newsOnDiscord ? "VIEW NEWS OF NEW SHARED HIGHLIGHTS ON DISCORD" : "");
        set_text(entry_at(RateHighlightsMenu_menuHint), s_hilHint);
    }
    if (s_jobKind == JOB_RATE_SEND && s_jobState >= 2) {    /* (sent: the menu closes) */
        s_sendFailed = s_jobState == 3;
        s_jobState = 0;
        SetExitMenuFlag();
    }
}

static void view_server(const Hil *h);

static void RateHighlights_Filename_onSelect(void)
{
    const int32_t idx = s_hilPage * kRows + selected_ordinal() - RateHighlightsMenu_filename_01;
    if (s_jobState != 1 && idx >= 0 && idx < s_hilN)
        view_server(&s_hil[idx]);
}

static void RateHighlights_Rate_onSelect(void)
{
    const int32_t idx = s_hilPage * kRows + selected_ordinal() - RateHighlightsMenu_rate_01;
    if (s_jobState == 1 || idx < 0 || idx >= s_hilN)
        return;
    Hil *h = &s_hil[idx];
    if (!SDL_strcasecmp(h->user, s_me)) {
        tell("RATE HIGHLIGHT", "HEY, THIS IS NOT A SELF-ASSESSMENT...|LET SOMEBODY ELSE RATE YOUR HIGHLIGHT.");
        return;
    }
    h->rate = h->rate == 0.0 ? 3.0 : h->rate + 0.5;    /* (changeRate: 3.0 first, up to 5.0, then off) */
    if (h->rate > 5.0)
        h->rate = 0.0;
}

static void RateHighlights_ScrollUp_onSelect(void) { s_hilPage = (s_hilPage + pages(s_hilN) - 1) % pages(s_hilN); }
static void RateHighlights_ScrollDown_onSelect(void) { s_hilPage = (s_hilPage + 1) % pages(s_hilN); }

static void RateHighlights_menuExit_onSelect(void)
{
    if (s_jobState == 1)
        return;
    if (job_start(JOB_RATE_SEND) != 0)      /* (nothing loaded, no network: just out) */
        SetExitMenuFlag();
}

/* ---- SHARE HIGHLIGHTS ---- */

static void note_file(const char *hil, char *out, uint32_t cap)    /* NAME.HIL -> NAME.TXT */
{
    const char *dot = strrchr(hil, '.');
    snprintf(out, cap, "%.*s.TXT", (int)(dot ? dot - hil : (long)strlen(hil)), hil);
    upper(out);
}

static void note_save(const Hil *h)     /* (saveNote: the comment, then the scene) */
{
    char name[128], text[128];
    note_file(h->file, name, sizeof name);
    snprintf(text, sizeof text, "%s\n%d\n", h->note, (int)h->scene);
    ModApi_SaveFile(SLUG, name, text, (uint32_t)strlen(text));
}

static int32_t list_hil(const char *name, void *ctx)
{
    (void)ctx;
    const size_t n = strlen(name);
    if (n < 5 || SDL_strcasecmp(name + n - 4, ".HIL") != 0 || s_hilN >= 2000)
        return 0;
    if (!SDL_strcasecmp(name, "GALORE.HIL"))    /* (a downloaded highlight's copy, for the viewer) */
        return 0;
    Hil *h = &s_hil[s_hilN++];
    memset(h, 0, sizeof *h);
    snprintf(h->user, sizeof h->user, "%s", s_me);
    snprintf(h->file, sizeof h->file, "%s", name);
    return 0;
}

static int hil_cmp(const void *a, const void *b) { return SDL_strcasecmp(((const Hil *)a)->file, ((const Hil *)b)->file); }

/* getLocalHilData: the files, their comments and scenes, whether each is shared already */
static void share_local(void)
{
    s_hilN = 0;
    if (!hil_alloc())
        return;
#if defined(SWOS_HTTPS)
    ModApi_ListFiles(swos_save_dir(), list_hil, NULL);
#endif
    qsort(s_hil, (size_t)s_hilN, sizeof(Hil), hil_cmp);
    {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char stamp[24] = "";
        if (tm)
            strftime(stamp, sizeof stamp, "%Y-%m-%d-%H-%M-%S", tm);
        for (int32_t i = 0; i < s_hilN; i++) {
            Hil *h = &s_hil[i];
            char name[128], path[1024];
            uint32_t size = 0;
            snprintf(h->datetime, sizeof h->datetime, "%s", stamp);
#if defined(SWOS_HTTPS)
            snprintf(path, sizeof path, "%s/%s", swos_save_dir(), h->file);
            uint8_t *data = (uint8_t *)ModApi_ReadFile(path, &size);
            h->maxScenes = hil_scenes(data, size, &h->style);
            free(data);
#else
            (void)path;
#endif
            note_file(h->file, name, sizeof name);
            char *text = (char *)ModApi_LoadFile(SLUG, name, &size);
            const int32_t noted = text != NULL;
            if (text) {
                char *nl = strchr(text, '\n');
                if (nl)
                    *nl = 0;
                snprintf(h->note, sizeof h->note, "%.36s", text);
                h->scene = nl ? atoi(nl + 1) : 0;
                free(text);
            }
            /* shared already: the server has its file under this player (getLocalHilData) */
            char want[160];
            snprintf(want, sizeof want, "\t%s\t", h->file);
            for (const char *line = s_jobText; line && *line;) {
                const char *tab = strchr(line, '\t'), *nl = strchr(line, '\n');
                if (tab && (!nl || tab < nl) && !strncmp(tab, want, strlen(want))) {
                    h->shared = h->sharedWas = 1;
                    break;
                }
                line = nl ? nl + 1 : NULL;
            }
            snprintf(h->noteWas, sizeof h->noteWas, "%s", h->note);
            h->sceneWas = h->scene;
            /* (the server's comment and scene, where it has the row, are what EXIT compares with) */
            if (h->sharedWas)
                for (const char *line = s_jobText; line && *line;) {
                    const char *nl = strchr(line, '\n');
                    char row[400], *f[5];
                    int32_t n = 0;
                    snprintf(row, sizeof row, "%.*s", (int)(nl ? nl - line : (long)strlen(line)), line);
                    for (char *p = row; p && n < 5; n++) {
                        f[n] = p;
                        p = strchr(p, '\t');
                        if (p)
                            *p++ = 0;
                    }
                    if (n == 5 && !strcmp(f[1], h->file)) {
                        snprintf(h->noteWas, sizeof h->noteWas, "%s", f[2]);
                        h->sceneWas = atoi(f[4]);
                        if (!noted) {   /* (shared from elsewhere: nothing kept here -- the
                                         *  server's, or EXIT asks for a comment it has, and
                                         *  puts ALL over its scene) */
                            snprintf(h->note, sizeof h->note, "%s", h->noteWas);
                            h->scene = h->sceneWas;
                        }
                        break;
                    }
                    line = nl ? nl + 1 : NULL;
                }
        }
    }
}

/* The player's shares with no file here (deleted, renamed, shared from another computer): listed
 * after the files, so that they can be seen and switched off -- nothing is taken off the site unless
 * its SHARE is (SWOS 2020 deleted their rows at every EXIT, asked or not). */
static void share_away(void)
{
    const int32_t local = s_hilN;
    for (const char *line = s_jobText; line && *line && s_hilN < 2000;) {
        const char *nl = strchr(line, '\n');
        char row[400], *f[5];
        int32_t n = 0, here = 0;
        snprintf(row, sizeof row, "%.*s", (int)(nl ? nl - line : (long)strlen(line)), line);
        for (char *p = row; p && n < 5; n++) {
            f[n] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = 0;
        }
        line = nl ? nl + 1 : NULL;
        if (n != 5 || !f[1][0])
            continue;
        for (int32_t i = 0; i < s_hilN && !here; i++)
            here = !strcmp(s_hil[i].file, f[1]);
        if (here)
            continue;
        Hil *h = &s_hil[s_hilN++];
        memset(h, 0, sizeof *h);
        snprintf(h->user, sizeof h->user, "%s", f[0]);      /* (as the site spells it: its folder) */
        snprintf(h->file, sizeof h->file, "%s", f[1]);
        snprintf(h->note, sizeof h->note, "%s", f[2]);
        snprintf(h->noteWas, sizeof h->noteWas, "%s", f[2]);
        snprintf(h->datetime, sizeof h->datetime, "%s", f[3]);
        h->scene = h->sceneWas = atoi(f[4]);
        h->shared = h->sharedWas = h->away = 1;
    }
    qsort(s_hil + local, (size_t)(s_hilN - local), sizeof(Hil), hil_cmp);
}

static int32_t num_shared(void)
{
    int32_t n = 0;
    for (int32_t i = 0; i < s_hilN; i++)
        n += s_hil[i].shared != 0;
    return n;
}

static void shareHighlightsMenu_OnInit(void)
{
    s_hilMode = 2;
    s_hilN = 0;
    s_hilPage = 0;
    s_shareUnknown = 0;
    if (job_start(JOB_SHARE_LOAD) != 0)
        share_local();
}
static void shareHighlightsMenu_OnReturn(void) {}

static void shareHighlightsMenu_OnDraw(void)
{
    static const char *const heads[4] = { " HIGHLIGHT FILE", "                  COMMENT", "SHARE", "SCENE" };
    static const int32_t col0[4] = { ShareHighlightsMenu_filename_00, ShareHighlightsMenu_note_00,
                                     ShareHighlightsMenu_shared_00, ShareHighlightsMenu_scene_00 };
    static const int32_t widths[2] = { ShareHighlightsMenu_kSH_FilenameWidth, ShareHighlightsMenu_kSH_NoteWidth };
    if (s_jobKind == JOB_SHARE_LOAD && s_jobState >= 2) {
        s_shareUnknown = s_jobState == 3;
        if (s_shareUnknown)
            s_jobText[0] = 0;           /* (no answer: nothing counted as shared) */
        s_jobState = 0;
        share_local();
        if (s_hil)
            share_away();
    }
    for (int32_t c = 0; c < 4; c++) {
        set_text(entry_at(col0[c]), heads[c]);
        set_back(entry_at(col0[c]), kLightBrownWithOrangeFrame);
    }
    snprintf(s_hilTitle, sizeof s_hilTitle, "SHARE HIGHLIGHTS (%d)", (int)s_hilN);
    set_text(entry_at(ShareHighlightsMenu_header), s_hilTitle);
    if (s_hilPage >= pages(s_hilN))
        s_hilPage = 0;
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t idx = s_hilPage * kRows + i;
        const Hil *h = idx < s_hilN ? &s_hil[idx] : NULL;
        for (int32_t c = 0; c < 4; c++)
            s_hilCells[i][c][0] = 0;
        if (h) {
            snprintf(s_hilCells[i][0], 64, "%s", h->file);
            snprintf(s_hilCells[i][1], 64, "%s", h->note);
            for (int32_t c = 0; c < 2; c++)
                shorten_to(s_hilCells[i][c], entry_at(col0[c] + 1 + i), widths[c] - 8);
            snprintf(s_hilCells[i][2], 64, "%s", h->shared ? "ON" : "OFF");
            if (h->scene == 0)
                snprintf(s_hilCells[i][3], 64, "ALL");
            else
                snprintf(s_hilCells[i][3], 64, "%d", (int)h->scene);
        }
        for (int32_t c = 0; c < 4; c++)
            set_text(entry_at(col0[c] + 1 + i), s_hilCells[i][c]);
        set_back(entry_at(ShareHighlightsMenu_shared_01 + i), !strcmp(s_hilCells[i][2], "ON") ? kGreenBack : kDarkBlue);
        set_back(entry_at(ShareHighlightsMenu_scene_01 + i),
                 s_hilCells[i][3][0] && !(h && h->away) ? kLightBlue : kDarkBlue);
        set_color(entry_at(ShareHighlightsMenu_filename_01 + i), h && h->away ? kYellowText : 0);
    }
    snprintf(s_hilPageText, sizeof s_hilPageText, "(%d OF %d)", (int)s_hilPage + 1, (int)pages(s_hilN));
    set_text(entry_at(ShareHighlightsMenu_page), s_hilPageText);
    entry_at(ShareHighlightsMenu_scrollUp)->afterDrawCb = hilArrow_AfterDraw;
    entry_at(ShareHighlightsMenu_scrollDown)->afterDrawCb = hilArrow_AfterDraw;
    table_nav(col0, ShareHighlightsMenu_scrollUp, ShareHighlightsMenu_scrollDown, ShareHighlightsMenu_menuExit,
              ShareHighlightsMenu_newsOnDiscord);
    {
        const int32_t m = selected_ordinal();
        int32_t row = -1, col = -1;
        for (int32_t c = 0; c < 4; c++)
            if (m >= col0[c] + 1 && m <= col0[c] + kRows) {
                row = m - col0[c] - 1;
                col = c;
            }
        const int32_t idx = s_hilPage * kRows + row;
        s_hilDesc[0] = 0;
        if (s_jobState == 1 && s_jobKind == JOB_SHARE_LOAD)
            snprintf(s_hilDesc, sizeof s_hilDesc, "LOADING...");
        else if (s_jobState == 1 && s_jobKind == JOB_SHARE_SEND)
            snprintf(s_hilDesc, sizeof s_hilDesc, s_jobPhase == 0 ? "DELETING SERVER DATA (%d%%)..."
                                                  : s_jobPhase == 2 ? "UPLOADING HIGHLIGHT FILE... (%d%%)"
                                                                    : "UPDATING SERVER DATA (%d%%)...", (int)job_percent());
        else if (s_shareUnknown)
            snprintf(s_hilDesc, sizeof s_hilDesc, "SENSIBLESOCCER.DE CANNOT BE REACHED");
        else if (row >= 0 && idx < s_hilN)
            snprintf(s_hilDesc, sizeof s_hilDesc, "%s%s", s_hil[idx].file,
                     s_hil[idx].away ? " - NOT ON THIS COMPUTER" : "");
        set_text(entry_at(ShareHighlightsMenu_longDesc), s_hilDesc);
        if (m == ShareHighlightsMenu_menuExit)
            snprintf(s_hilHint, sizeof s_hilHint, "HIGHLIGHTS SHARING OPTIONS ARE SUBMITTED UPON EXIT");
        else if ((col == 1 || col == 3) && idx < s_hilN && s_hil[idx].away)
            snprintf(s_hilHint, sizeof s_hilHint, "NOT ON THIS COMPUTER: IT CAN BE VIEWED OR SWITCHED OFF");
        else if (col == 0)
            snprintf(s_hilHint, sizeof s_hilHint, "HIT FIRE TO VIEW THE HIGHLIGHT");
        else if (col == 1)
            snprintf(s_hilHint, sizeof s_hilHint, "HIT FIRE TO EDIT THE COMMENT");
        else if (col == 3)
            snprintf(s_hilHint, sizeof s_hilHint, "HIT FIRE TO SELECT SCENE(S) FOR PROMOTION ON SWOS GALORE");
        else if (m == ShareHighlightsMenu_newsOnDiscord)
            snprintf(s_hilHint, sizeof s_hilHint, "VIEW NEWS OF NEW SHARED HIGHLIGHTS ON DISCORD");
        else if (col == 2)
            snprintf(s_hilHint, sizeof s_hilHint, "HIT FIRE TO TOGGLE THE SHARE OPTION. NUM OF SHARED IS %d.",
                     (int)num_shared());
        else
            s_hilHint[0] = 0;
        set_text(entry_at(ShareHighlightsMenu_numShared), s_hilHint);
    }
    if (s_jobKind == JOB_SHARE_SEND && s_jobState >= 2) {
        s_sendFailed = s_jobState == 3;
        s_jobState = 0;
        SetExitMenuFlag();
    }
}

static void view_local(const Hil *h);

static int32_t share_row(int32_t first)
{
    const int32_t idx = s_hilPage * kRows + selected_ordinal() - first;
    return s_jobState != 1 && idx >= 0 && idx < s_hilN ? idx : -1;
}

static void ShareHighlights_Filename_onSelect(void)
{
    const int32_t i = share_row(ShareHighlightsMenu_filename_01);
    if (i >= 0 && s_hil[i].away)
        view_server(&s_hil[i]);             /* (downloaded: there is no file of it here) */
    else if (i >= 0)
        view_local(&s_hil[i]);
}

static void ShareHighlights_Note_onSelect(void)     /* enterNote: 36 characters, kept at once */
{
    const int32_t i = share_row(ShareHighlightsMenu_note_01);
    if (i < 0 || s_hil[i].away)
        return;
    static char buf[40];
    MenuEntry *e = (MenuEntry *)CalcMenuEntryAddress((uint16_t)selected_ordinal());
    snprintf(buf, sizeof buf, "%s", s_hil[i].note);
    e->fg = (uintptr_t)buf;
    if (InputText(37, (uint8_t *)e) == 0) {
        snprintf(s_hil[i].note, sizeof s_hil[i].note, "%s", buf);
        note_save(&s_hil[i]);
    }
}

static void ShareHighlights_Shared_onSelect(void)
{
    const int32_t i = share_row(ShareHighlightsMenu_shared_01);
    if (i >= 0)
        s_hil[i].shared = !s_hil[i].shared;
}

static void ShareHighlights_Scene_onSelect(void)    /* changeScene: ALL, 1 .. n, kept at once */
{
    const int32_t i = share_row(ShareHighlightsMenu_scene_01);
    if (i < 0 || s_hil[i].away)             /* (how many scenes it has is in the file) */
        return;
    if (++s_hil[i].scene > s_hil[i].maxScenes)
        s_hil[i].scene = 0;
    note_save(&s_hil[i]);
}

static void ShareHighlights_ScrollUp_onSelect(void) { s_hilPage = (s_hilPage + pages(s_hilN) - 1) % pages(s_hilN); }
static void ShareHighlights_ScrollDown_onSelect(void) { s_hilPage = (s_hilPage + 1) % pages(s_hilN); }

static void ShareHighlights_menuExit_onSelect(void)
{
    if (s_jobState == 1)
        return;
    for (int32_t i = 0; i < s_hilN; i++)
        if (s_hil[i].shared && !s_hil[i].away) {    /* (an away one is sent nothing, only deleted) */
            if (!s_hil[i].note[0]) {
                tell("WARNING", "SORRY. COMMENT SHOULD BE ENTERED|TO SHARE HIGHLIGHTS");
                return;
            }
            if (!SDL_strcasecmp(s_hil[i].file, "LATEST.HIL")) {
                tell("WARNING", "SORRY. LATEST.HIL IS NOT ALLOWED|TO PREVENT DUPLICATED FILES IN SERVER");
                return;
            }
        }
    if (job_start(JOB_SHARE_SEND) != 0)
        SetExitMenuFlag();
}

/* ---- VIEW HIGHLIGHT ---- */

static char    s_vhFile[110], s_vhNote[70], s_vhStyleText[32], s_vhSceneText[32];
static int32_t s_vhStyle, s_vhStyleKnown, s_vhScene, s_vhMaxScenes, s_vhOther, s_vhYes;

static void viewHighlightMenu_OnInit(void) {}

static void viewHighlightMenu_OnDraw(void)
{
    static const char *const names[5] = { "PC DOS (70 FPS)", "AMIGA (50 FPS)", "MOON SOCCER (50 FPS)", "AMIGA (60 FPS)",
                                          "XBOX (60 FPS)" };
    set_text(entry_at(ViewHighlightMenu_message2), s_vhFile);
    set_text(entry_at(ViewHighlightMenu_message3), s_vhNote);
    snprintf(s_vhStyleText, sizeof s_vhStyleText, "%s %s", names[s_vhStyle], s_vhStyleKnown ? "" : "(?)");
    set_text(entry_at(ViewHighlightMenu_fpsOption), s_vhStyleText);
    if (s_vhScene == 0)
        snprintf(s_vhSceneText, sizeof s_vhSceneText, "ALL %d SCENE%s", (int)s_vhMaxScenes, s_vhMaxScenes == 1 ? "" : "S");
    else
        snprintf(s_vhSceneText, sizeof s_vhSceneText, "SCENE %d OF %d", (int)s_vhScene, (int)s_vhMaxScenes);
    set_text(entry_at(ViewHighlightMenu_sceneOption), s_vhSceneText);
    /* another player's highlight with its scene chosen: that scene, not to be changed */
    const int32_t locked = s_vhOther && s_vhScene != 0;
    MenuEntry *fps = entry_at(ViewHighlightMenu_fpsOption), *scene = entry_at(ViewHighlightMenu_sceneOption),
              *yes = entry_at(ViewHighlightMenu_yes);
    fps->downEntry = (int8_t)(locked ? ViewHighlightMenu_yes : ViewHighlightMenu_sceneOption);
    scene->upEntry = (int8_t)(locked ? -1 : ViewHighlightMenu_fpsOption);
    scene->downEntry = (int8_t)(locked ? -1 : ViewHighlightMenu_yes);
    scene->isDisabled = (uint8_t)locked;
    set_back(scene, locked ? kGray : kLightBlue);
    yes->upEntry = (int8_t)(locked ? ViewHighlightMenu_fpsOption : ViewHighlightMenu_sceneOption);
}

static void viewHighlightMenu_fpsOption_OnSelect(void)     /* 0, 1, 3, 4 (MOON SOCCER skipped) */
{
    s_vhStyle = s_vhStyle == 1 ? 3 : (s_vhStyle + 1) % 5;
}
static void viewHighlightMenu_sceneOption_OnSelect(void)
{
    if (s_vhOther && s_vhScene != 0)
        return;
    if (++s_vhScene > s_vhMaxScenes)
        s_vhScene = 0;
}
static void viewHighlightMenu_yes_OnSelect(void) { s_vhYes = 1; SetExitMenuFlag(); }
static void viewHighlightMenu_no_OnSelect(void) { s_vhYes = 0; SetExitMenuFlag(); }

/* The fire that chose a highlight, let go before the box opens -- a download keeps the events
 * waiting, and a fire still down would be the box's first choice (its EXIT). A second at most. */
static void fire_released(void)
{
    extern volatile uint16_t _fire;
    extern void MenuCheckControls(void);
    extern void WaitForVSync(void);
    for (int32_t i = 0; i < 70; i++) {
        WaitForVSync();
        MenuCheckControls();
        if (!_fire)
            break;
    }
}

/* The box, then the highlight (in the save folder's mods/galore/view.HIL) at the rate chosen */
static void view_box(const char *file, const char *note, int32_t scene, int32_t other)
{
    static const int32_t kFps[5] = { 70, 50, 50, 60, 60 };
    uint32_t size = 0;
    uint8_t *data = (uint8_t *)ModApi_LoadFile("galore", "view.HIL", &size);
    int32_t style = -1;
    s_vhMaxScenes = hil_scenes(data, size, &style);
    free(data);
    if (!s_vhMaxScenes) {
        static const char *const kOk[1] = { "OK" };
        ModApi_ShowDialog("VIEW HIGHLIGHT", "HIGHLIGHT FILE IS NOT AVAILABLE", kOk, 1, 0);
        return;
    }
    snprintf(s_vhFile, sizeof s_vhFile, "- %s -", file);
    snprintf(s_vhNote, sizeof s_vhNote, "%s", note);
    s_vhStyleKnown = style >= 0;
    s_vhStyle = style >= 0 ? style : 1;
    s_vhScene = scene <= s_vhMaxScenes ? scene : 0;
    s_vhOther = other;
    s_vhYes = 0;
    fire_released();
    ModApi_ShowMenu("swos2020/viewHighlightMenu");     /* (the keys drained: the press that chose it
                                                         * is not the box's first) */
    if (s_vhYes && ModApi_ViewHighlightsAt("galore", "view.HIL", s_vhScene, kFps[s_vhStyle]) != 0) {
        static const char *const kOk[1] = { "OK" };
        ModApi_ShowDialog("VIEW HIGHLIGHT", "HIGHLIGHT FILE IS NOT AVAILABLE", kOk, 1, 0);
    }
}

/* viewServerHilFileMsgBox: downloaded first (the online-records service), then the box */
static void view_server(const Hil *h)
{
    char args[256], u[64], f[160], out[32];
    snprintf(args, sizeof args, "{\"user\":\"%s\",\"file\":\"%s\"}", ModApi_JsonEscape(h->user, u, sizeof u),
             ModApi_JsonEscape(h->file, f, sizeof f));
    if (download("galore.download", args, out, sizeof out) != 0) {
        tell("VIEW HIGHLIGHT", "HIGHLIGHT FILE IS NOT AVAILABLE");
        return;
    }
    view_box(h->file, h->note, h->scene, SDL_strcasecmp(h->user, s_me) != 0);
}

/* viewLocalHilFileMsgBox: the player's own file, copied for the viewer, then the box */
static void view_local(const Hil *h)
{
#if defined(SWOS_HTTPS)
    char path[1024];
    uint32_t size = 0;
    snprintf(path, sizeof path, "%s/%s", swos_save_dir(), h->file);
    uint8_t *data = (uint8_t *)ModApi_ReadFile(path, &size);
    const int32_t ok = data && ModApi_SaveFile("galore", "view.HIL", data, size) == 0;
    free(data);
    if (ok)
        view_box(h->file, h->note, h->scene, 0);
    else
        tell("VIEW HIGHLIGHT", "HIGHLIGHT FILE IS NOT AVAILABLE");
#else
    (void)h;
#endif
}

/* ---- the way in: SWOS 2020's selectLatestUploads / selectShareHighlights ---- */

/* After RATE or SHARE has closed: what its EXIT could not send is said, not left unnoticed. */
static void tell_unsent(void)
{
    if (s_sendFailed)
        tell("SWOS GALORE", "SOME CHANGES COULD NOT BE SENT TO|SENSIBLESOCCER.DE. PLEASE TRY AGAIN.");
    s_sendFailed = 0;
}

static void open_monthly(void) { s_monthly = 1; s_monthIndex = 0; show("swos2020/chartHighlightsMenu"); }

/* SERVER LOGIN first; 1 logged in (s_me set) */
static int32_t galore_login(void)
{
#if defined(SWOS_HTTPS)
    if (OnlineMenus_ServerLogIn(LOGIN_FOR_GALORE, United_UserName(), open_monthly) != 0 || !United_UserName()[0])
        return 0;
    snprintf(s_me, sizeof s_me, "%s", United_UserName());
    return 1;
#else
    ModApi_ShowError("SWOS GALORE NEEDS A BUILD THAT CAN GO ONLINE");
    return 0;
#endif
}

static void open_url_discord(void)
{
#if defined(SWOS_HTTPS)
    Platform_OpenUrl("https://discord.gg/dcTdk6H4fz");   /* (SWOS 2020's newsOnDiscordSWOSGalore_OnSelect) */
#endif
}

static void selectLatestUploads(void)
{
    static const char *const kExit[1] = { "EXIT" };
    if (!galore_login())
        return;
    if (United_Banned(s_me, "SWOS GALORE")) {     /* (SWOS 2020's showFeatureNotAvailableMessage) */
        ModApi_ShowDialog("WARNING", "YOU ARE BANNED FROM SWOS GALORE.|PLEASE CONTACT THE SITE MANAGER.", kExit, 1, 0);
        return;
    }
    s_sendFailed = 0;
    show("swos2020/rateHighlightsMenu");
    tell_unsent();
}

static void selectShareHighlights(void)
{
    if (!galore_login())
        return;
    s_sendFailed = 0;
    show("swos2020/shareHighlightsMenu");
    tell_unsent();
}

static void newsOnDiscordSWOSGalore_OnSelect(void) { open_url_discord(); }

/* ---- the mod ---- */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(swosGaloreMenu_OnDraw), HANDLER(selectAllTimeChart), HANDLER(selectMonthlyChart),
    HANDLER(selectLatestUploads), HANDLER(selectShareHighlights),
    HANDLER(ChartHighlightsMenu_OnInit), HANDLER(ChartHighlightsMenu_OnDraw), HANDLER(ChartHighlightsMenu_OnReturn),
    HANDLER(ChartHighlights_Filename_onSelect), HANDLER(ChartHighlights_ScrollUp_onSelect),
    HANDLER(ChartHighlights_ScrollDown_onSelect), HANDLER(ChartHighlights_menuExit_onSelect),
    HANDLER(drawStarIconsForChartHighlights), HANDLER(newsOnDiscordSWOSGalore_OnSelect),
    HANDLER(yearMonth_Dec_onSelect), HANDLER(yearMonth_Inc_onSelect),
    HANDLER(RateHighlightsMenu_OnInit), HANDLER(RateHighlightsMenu_OnDraw), HANDLER(RateHighlightsMenu_OnReturn),
    HANDLER(RateHighlights_Filename_onSelect), HANDLER(RateHighlights_Rate_onSelect),
    HANDLER(RateHighlights_ScrollUp_onSelect), HANDLER(RateHighlights_ScrollDown_onSelect),
    HANDLER(RateHighlights_menuExit_onSelect),
    HANDLER(shareHighlightsMenu_OnInit), HANDLER(shareHighlightsMenu_OnDraw), HANDLER(shareHighlightsMenu_OnReturn),
    HANDLER(ShareHighlights_Filename_onSelect), HANDLER(ShareHighlights_Note_onSelect),
    HANDLER(ShareHighlights_Shared_onSelect), HANDLER(ShareHighlights_Scene_onSelect),
    HANDLER(ShareHighlights_ScrollUp_onSelect), HANDLER(ShareHighlights_ScrollDown_onSelect),
    HANDLER(ShareHighlights_menuExit_onSelect),
    HANDLER(viewHighlightMenu_OnInit), HANDLER(viewHighlightMenu_OnDraw), HANDLER(viewHighlightMenu_fpsOption_OnSelect),
    HANDLER(viewHighlightMenu_sceneOption_OnSelect), HANDLER(viewHighlightMenu_yes_OnSelect),
    HANDLER(viewHighlightMenu_no_OnSelect),
};
#undef HANDLER

static void galore_open(void) { show("swos2020/swosGaloreMenu"); }

/* SWOS_GALORE_TEST=<rank>[:month]: the chart fetched, that place's highlight downloaded and shown
 * (its scene), then exit -- GALORE TEST ... */
static void galore_test(void)
{
    static int32_t done, drawn;
    if (done || ++drawn < 10)           /* (not at the first draw: the front end is still being
                                         *  set up then, and a highlight shown there crashed) */
        return;
    const char *t = getenv("SWOS_GALORE_TEST");
    /* SWOS_GALORE_SCREEN_TEST=rate|share (with a test log-in, SWOS_UNITED_LOGIN): at the first menu
     * frame, RATE or SHARE as SWOS GALORE opens it (its SERVER LOGIN first, then the screen:
     * SWOS_KEY_SCRIPT drives both). Reads only, until EXIT is chosen. */
    const char *screen = getenv("SWOS_GALORE_SCREEN_TEST");
    if (!done && screen) {
        done = 1;
        if (!strcmp(screen, "share"))
            selectShareHighlights();
        else
            selectLatestUploads();
        printf("GALORE SCREEN TEST left\n");
        fflush(stdout);
        exit(0);
    }
    if (done || !t)
        return;
    done = 1;
    int32_t rc = 1;
    for (int32_t waited = 0; rc == 1 && waited < 30000; waited += 20) {
        rc = ModApi_CallService("online-records", "galore.chart", "{\"month\":\"\"}", s_chart, sizeof s_chart);
        if (rc == 1)
            SDL_Delay(20);
    }
    if (rc == 0)
        parse();
    const int32_t want = atoi(t);
    const Row *r = NULL;
    for (int32_t i = 0; i < s_numRows; i++)
        if (s_rows[i].rank == want)
            r = &s_rows[i];
    printf("GALORE TEST chart: %d highlights; #%d %s\n", (int)s_numRows, (int)want, r ? r->file : "(none)");
    if (r) {
        char args[256], u[64], f[160], out[32];
        snprintf(args, sizeof args, "{\"user\":\"%s\",\"file\":\"%s\"}", ModApi_JsonEscape(r->user, u, sizeof u),
                 ModApi_JsonEscape(r->file, f, sizeof f));
        rc = 1;
        for (int32_t waited = 0; rc == 1 && waited < 30000; waited += 20) {
            rc = ModApi_CallService("online-records", "galore.download", args, out, sizeof out);
            if (rc == 1)
                SDL_Delay(20);
        }
        printf("GALORE TEST download %s; scene %d\n", rc == 0 ? "ok" : "FAILED", (int)r->scene);
        fflush(stdout);
        if (rc == 0)
            rc = ModApi_ViewHighlights("galore", "view.HIL", r->scene);
        printf("GALORE TEST view %s\n", rc == 0 ? "ok" : "FAILED");
    }
    printf("GALORE TEST %s\n", r && rc == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    exit(r && rc == 0 ? 0 : 1);
}

static void galore2020_init(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    ModApi_AddEntry(MODPOINT_MODES, SLUG, "swos galore", galore_open);
}

const ModDef ModDef_galore2020 = {
    .slug = SLUG,
    .init = galore2020_init,
    .menuDrawn = galore_test,
};

#else
const ModDef ModDef_galore2020 = { .slug = "galore2020" };
#endif
