/*
 * united_galore.c -- SWOS United's SWOS GALORE (fork mod "galore2020"): SWOS 2020's shared goal
 * highlights -- its menu (ALL TIME CHART, MONTHLY CHART, RATE HIGHLIGHTS, SHARE HIGHLIGHTS) and
 * its chart (seven a page: rank, date, player, comment, the average rating in stars), a pick
 * downloaded and shown in OpenSWOS's highlights viewer (ModApi_ViewHighlights, its scene alone).
 * Its menus imported (data_json/menus/swos2020/, the importer's "SWOS Galore" group), their
 * handlers here under SWOS 2020's names (replays/replaysMenu.cpp).
 *
 * READS ONLY: the charts and the highlights come from sensiblesoccer.de (the online-records mod's
 * galore.chart / galore.download); RATE and SHARE would write there, and nothing from OpenSWOS
 * goes to sensiblesoccer.de (the owner, 2026-11-07) -- they wait for SWOS United's own server.
 * MODES -> SWOS GALORE.
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD)

#include "menu_load_json.h"
#include "menu_system.h"
#include "swos2020_menus.h"
#include "united_ui.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SLUG "galore2020"

extern void SetExitMenuFlag(void);
extern uint8_t _currentMenu[];

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

static void show(const char *name)
{
    uint8_t *menu = MenuJson_Load(name);
    if (menu)
        ShowMenu(menu);
    else
        fprintf(stderr, "[galore2020] cannot show %s\n", name);
}

/* ---- SWOS GALORE ---- */

static int32_t s_monthly, s_monthIndex;     /* the monthly chart: 0 this month, 11 a year ago */

static void swosGaloreMenu_OnDraw(void)
{
    const int32_t sel = selected_ordinal();
    set_text(entry_at(SwosGaloreMenu_desc),
             sel == SwosGaloreMenu_allTimeChart ? "THE BEST GOALS EVER SHARED"
             : sel == SwosGaloreMenu_monthlyChart ? "THE BEST GOALS OF A MONTH"
             : sel == SwosGaloreMenu_latestUploads ? "NOT YET: SWOS UNITED'S OWN SERVER"
             : sel == SwosGaloreMenu_shareHighlights ? "NOT YET: SWOS UNITED'S OWN SERVER" : "");
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
static void selectLatestUploads(void) { ModApi_ShowError("RATING COMES WITH SWOS UNITED'S OWN SERVER"); }
static void selectShareHighlights(void) { ModApi_ShowError("SHARING COMES WITH SWOS UNITED'S OWN SERVER"); }

/* ---- the chart ---- */

typedef struct { char user[32], shown[32], file[96], comment[64], date[12]; int32_t rank, scene, raters; double rate; } Row;
/* (user: as the server spells it -- its folders are case-sensitive; shown: in capitals) */
static Row    *s_rows;
static int32_t s_numRows, s_state, s_page, s_fresh;
static char    s_chart[1 << 18], s_title[48], s_month[40], s_monthArg[16], s_pageText[24], s_desc[96], s_hint[64];
static char    s_cells[kRows][4][40];

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
            snprintf(r->comment, sizeof r->comment, "%s", f[3]);
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

static void ChartHighlightsMenu_OnInit(void)
{
    s_state = 1;
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
    const int32_t pages = s_numRows ? (s_numRows + kRows - 1) / kRows : 1;
    if (s_page >= pages)
        s_page = pages - 1;
    for (int32_t i = 0; i < kRows; i++) {
        const int32_t idx = s_page * kRows + i;
        const Row *r = idx < s_numRows ? &s_rows[idx] : NULL;
        if (r) {
            snprintf(s_cells[i][0], 40, "%d", (int)r->rank);
            snprintf(s_cells[i][1], 40, "%s", r->date);
            snprintf(s_cells[i][2], 40, "%.10s", r->shown);
            snprintf(s_cells[i][3], 40, "%.17s", r->comment);
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
    snprintf(s_pageText, sizeof s_pageText, "(%d / %d)", (int)s_page + 1, (int)pages);
    set_text(entry_at(ChartHighlightsMenu_page), s_pageText);
    entry_at(ChartHighlightsMenu_scrollUp)->afterDrawCb = arrow_AfterDraw;
    entry_at(ChartHighlightsMenu_scrollDown)->afterDrawCb = arrow_AfterDraw;
    entry_at(ChartHighlightsMenu_newsOnDiscord)->isInvisible = 1;
    /* the picked one's comment in full, and what is happening */
    {
        const int32_t idx = s_page * kRows + selRow;
        if (s_state == 1)
            snprintf(s_hint, sizeof s_hint, "LOADING...");
        else if (s_state < 0)
            snprintf(s_hint, sizeof s_hint, "SENSIBLESOCCER.DE CANNOT BE REACHED");
        else if (!s_numRows)
            snprintf(s_hint, sizeof s_hint, "NO HIGHLIGHTS IN THIS CHART");
        else
            snprintf(s_hint, sizeof s_hint, "FIRE ON A HIGHLIGHT WATCHES IT");
        set_text(entry_at(ChartHighlightsMenu_menuHint), s_hint);
        if (!s_desc[0] || selRow >= 0) {
            if (selRow >= 0 && idx < s_numRows)
                snprintf(s_desc, sizeof s_desc, "%s (%.2f, %d RATER%s)", s_rows[idx].comment, s_rows[idx].rate,
                         (int)s_rows[idx].raters, s_rows[idx].raters == 1 ? "" : "S");
            else
                s_desc[0] = 0;
        }
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
    int32_t rc = 1;
    for (int32_t waited = 0; rc == 1 && waited < 30000; waited += 20) {
        rc = ModApi_CallService("online-records", "galore.download", args, out, sizeof out);
        if (rc == 1)
            SDL_Delay(20);
    }
    if (rc != 0) {
        snprintf(s_desc, sizeof s_desc, "THE HIGHLIGHT COULD NOT BE DOWNLOADED");
        return;
    }
    if (ModApi_ViewHighlights("galore", "view.HIL", s_rows[idx].scene) != 0)
        snprintf(s_desc, sizeof s_desc, "THE HIGHLIGHT COULD NOT BE PLAYED");
}

static void ChartHighlights_ScrollUp_onSelect(void) { if (s_page > 0) s_page--; }
static void ChartHighlights_ScrollDown_onSelect(void) { s_page++; }
static void ChartHighlights_menuExit_onSelect(void) { SetExitMenuFlag(); }
static void newsOnDiscordSWOSGalore_OnSelect(void) {}
static void yearMonth_Dec_onSelect(void)          /* (SWOS 2020's: DEC goes back a month) */
{
    if (s_monthIndex < 11)
        s_monthIndex++;
    s_state = 1;
    s_page = 0;
}
static void yearMonth_Inc_onSelect(void)
{
    if (s_monthIndex > 0)
        s_monthIndex--;
    s_state = 1;
    s_page = 0;
}

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
};
#undef HANDLER

static void galore_open(void) { show("swos2020/swosGaloreMenu"); }

/* SWOS_GALORE_TEST=<rank>[:month]: the chart fetched, that place's highlight downloaded and shown
 * (its scene), then exit -- GALORE TEST ... */
static void galore_test(void)
{
    static int32_t done;
    const char *t = getenv("SWOS_GALORE_TEST");
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
