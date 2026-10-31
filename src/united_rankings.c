/*
 * united_rankings.c -- RANKINGS (SWOS United): SWOS 2020's two screens, its rankings and
 * ranking menus (imported, data_json/menus/swos2020/), with their handlers here -- the log-in's
 * RANKINGS and the matchmaking screen's open the first, AMIGA or PC DOS the second, the online
 * ranking from sensiblesoccer.de (view=ranking&id=5 the Amiga's, 13 the PC's; XBOX left out).
 *
 * SWOS 2020's showRankingsMenu / getRanking / rankingMenu_OnDraw (src/mpgame.cpp): 18 players a
 * page under a header row -- place, name, nation, played, won, drawn, lost, SRN and its last
 * change -- the logged-in player's row picked out.
 */
#include "united_api.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>
#include "https_client.h"
#include "menu_load_json.h"
#include "menu_system.h"
#include "online_menus.h"
#include "swos2020_menus.h"

enum { ROWS = 18, MAX_PLAYERS = 4096 };
enum { kWhiteText2 = 2, kRedText = 10, kYellowText = 15, kLeftAligned = 1 << 15, kRightAligned = 1 << 14 };

typedef struct {
    char pos[6], name[20], nation[4], played[6], won[6], drawn[6], lost[6], srn[7], change[7];
} RankRow;

static int32_t  s_rankId;               /* 5 the Amiga's, 13 the PC's */
static RankRow *s_rows;
static int32_t  s_count, s_page, s_loading;
static char     s_status[48], s_pageText[16], s_date[12];
static char     s_cell[ROWS + 1][9][20];

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void set_cell(int32_t row, int32_t col, int32_t ordinal, const char *text, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    snprintf(s_cell[row][col], sizeof s_cell[row][col], "%s", text);
    e->fg = (uintptr_t)s_cell[row][col];
    e->stringFlags = (uint16_t)((e->stringFlags & (kLeftAligned | kRightAligned)) | color);
}

/* ---- the reply ---- */

static void parse(const char *xml, uint32_t len)
{
    const char *end = xml + len, *p = xml, *blockEnd, *b;
    s_count = 0;
    if (!s_rows)
        s_rows = (RankRow *)malloc(MAX_PLAYERS * sizeof *s_rows);
    if (!s_rows)
        return;
    while (s_count < MAX_PLAYERS && (b = United_XmlBlock(p, end, "player", &blockEnd)) != NULL) {
        RankRow *r = &s_rows[s_count];
        char v[64];
        uint32_t i;
        United_XmlText(b, blockEnd, "pos", r->pos, sizeof r->pos);
        United_XmlText(b, blockEnd, "username", v, sizeof v);
        for (i = 0; v[i] && i < 8; i++)     /* (the menu font: capitals; 8 fit the column, as SWOS 2020) */
            r->name[i] = (char)(v[i] >= 'a' && v[i] <= 'z' ? v[i] - 'a' + 'A' : v[i]);
        r->name[i] = 0;
        United_XmlText(b, blockEnd, "nation", v, sizeof v);
        for (i = 0; v[i] && i < 3; i++)
            r->nation[i] = (char)(v[i] >= 'a' && v[i] <= 'z' ? v[i] - 'a' + 'A' : v[i]);
        r->nation[i] = 0;
        United_XmlText(b, blockEnd, "played", r->played, sizeof r->played);
        United_XmlText(b, blockEnd, "wins", r->won, sizeof r->won);
        United_XmlText(b, blockEnd, "draws", r->drawn, sizeof r->drawn);
        United_XmlText(b, blockEnd, "losts", r->lost, sizeof r->lost);
        United_XmlText(b, blockEnd, "srn", r->srn, sizeof r->srn);
        United_XmlText(b, blockEnd, "srnc", r->change, sizeof r->change);
        s_count++;
        p = blockEnd;
    }
}

/* ---- rankingsMenu: AMIGA, PC DOS ---- */

static void rankingsMenu_OnInit(void)
{
    entry_at(RankingsMenu_xbla)->isInvisible = 1;       /* (no XBOX, for now: the document) */
}

static void rankingsMenu_OnDraw(void)
{
    entry_at(RankingsMenu_xbla)->isInvisible = 1;
}

static void show_ranking(int32_t id)
{
    s_rankId = id;
    OnlineMenus_Show("swos2020/rankingMenu");
}

static void selectAmigaRankingsMenu(void) { show_ranking(5); }
static void selectPCDOSRankingsMenu(void) { show_ranking(13); }

/* ---- rankingMenu ---- */

static void rankingMenu_OnInit(void)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    s_count = s_page = 0;
    snprintf(s_status, sizeof s_status, "LOADING...");
    if (t)
        snprintf(s_date, sizeof s_date, "%02d/%02d/%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    {
        char query[64];
        snprintf(query, sizeof query, "id=%d&format=xml", (int)s_rankId);
        s_loading = Https_RequestAsync(United_ApiUrl("ranking", query), NULL) == 0;
    }
    if (!s_loading)
        snprintf(s_status, sizeof s_status, "BUSY - TRY AGAIN");
}

static void rankingMenu_OnDraw(void)
{
    static const char *const head[9] = { "", "PLAYER", "", "PL", "W", "D", "L", "SRN", "+/-" };
    static const int32_t col0[9] = {
        RankingMenu_pos_000, RankingMenu_username_000, RankingMenu_nation_000, RankingMenu_played_000,
        RankingMenu_wins_000, RankingMenu_draws_000, RankingMenu_losts_000, RankingMenu_srn_000,
        RankingMenu_srnc_000,
    };
    const char *me = United_UserName();
    int32_t row, c, pages;

    if (s_loading) {
        HttpsReply reply;
        if (Https_PollAsync(&reply)) {
            s_loading = 0;
            if (reply.status == 200 && reply.body) {
                parse(reply.body, reply.length);
                s_status[0] = 0;
                if (!s_count)
                    snprintf(s_status, sizeof s_status, "NO PLAYERS IN THIS RANKING");
            } else {
                snprintf(s_status, sizeof s_status, "CANNOT REACH SENSIBLESOCCER.DE");
            }
            Https_Free(&reply);
        }
    }

    entry_at(RankingMenu_header)->fg = (uintptr_t)(s_rankId == 13 ? "PC DOS RANKING" : "AMIGA RANKING");
    entry_at(RankingMenu_timeStamp)->fg = (uintptr_t)s_date;
    pages = s_count ? (s_count + ROWS - 1) / ROWS : 1;
    if (s_page >= pages)
        s_page = pages - 1;
    snprintf(s_pageText, sizeof s_pageText, "(%d / %d)", (int)s_page + 1, (int)pages);
    entry_at(RankingMenu_ranking_Page)->fg = (uintptr_t)s_pageText;
    entry_at(RankingMenu_desc)->fg = (uintptr_t)s_status;
    /* The page arrows: SWOS 2020 draws its own; here, small boxes with the direction in them. */
    {
        MenuEntry *up = entry_at(RankingMenu_ranking_ScrollUp), *down = entry_at(RankingMenu_ranking_ScrollDown);
        up->background = down->background = ENTRY_FRAME_AND_BACK_COLOR;
        up->bgColor = down->bgColor = 10;   /* (red, as the lobby chat's) */
        up->fg = (uintptr_t)"-";
        down->fg = (uintptr_t)"+";
    }

    for (c = 0; c < 9; c++)
        set_cell(0, c, col0[c], head[c], kWhiteText2);
    for (row = 1; row <= ROWS; row++) {
        const int32_t i = s_page * ROWS + row - 1;
        const RankRow *r = i < s_count ? &s_rows[i] : NULL;
        const uint16_t color = r && me[0] && SDL_strcasecmp(r->name, me) == 0 ? kYellowText : kWhiteText2;
        const char *v[9];
        if (r) {
            v[0] = r->pos; v[1] = r->name; v[2] = r->nation; v[3] = r->played; v[4] = r->won;
            v[5] = r->drawn; v[6] = r->lost; v[7] = r->srn; v[8] = r->change;
        } else {
            for (c = 0; c < 9; c++)
                v[c] = "";
        }
        for (c = 0; c < 9; c++)
            set_cell(row, c, col0[c] + row, v[c], color);
    }
}

static void rankingMenu_OnReturn(void) {}

static void ranking_ScrollUp_onSelect(void)
{
    if (s_page > 0)
        s_page--;
}

static void ranking_ScrollDown_onSelect(void)
{
    s_page++;                           /* (OnDraw keeps it in range) */
}

static void ranking_username_onSelect(void) {}     /* (a player's page: still to come) */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(rankingsMenu_OnInit),
    HANDLER(rankingsMenu_OnDraw),
    HANDLER(selectAmigaRankingsMenu),
    HANDLER(selectPCDOSRankingsMenu),
    HANDLER(rankingMenu_OnInit),
    HANDLER(rankingMenu_OnDraw),
    HANDLER(rankingMenu_OnReturn),
    HANDLER(ranking_ScrollUp_onSelect),
    HANDLER(ranking_ScrollDown_onSelect),
    HANDLER(ranking_username_onSelect),
};
#undef HANDLER

void UnitedRankings_Register(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

#else

void UnitedRankings_Register(void) {}

#endif
