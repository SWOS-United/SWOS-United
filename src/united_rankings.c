/*
 * united_rankings.c -- RANKINGS (SWOS United): SWOS 2020's two screens, its rankings and
 * ranking menus (imported, data_json/menus/swos2020/), with their handlers here -- the log-in's
 * RANKINGS and the matchmaking screen's open the first, AMIGA or PC DOS the second, the online
 * ranking from sensiblesoccer.de (view=ranking&id=5 the Amiga's, 13 the PC's, 8 the XBOX's -- SWOS 2020's downloadRankingFile) or,
 * with [mod.menus2020] account_site = swosunited, from swos.swosunited.com (api/rankings/users, the same
 * ids; its active players, as the old site lists them -- and no SRN change, which it does not keep).
 *
 * SWOS 2020's showRankingsMenu / getRanking / rankingMenu_OnDraw (src/mpgame.cpp): 18 players a
 * page under a header row -- place, name, nation, played, won, drawn, lost, SRN and its last
 * change -- the logged-in player's row picked out.
 */

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)

#include "united_api.h"     /* (SWOS-API's client: third_party/swos-api) */
#include "united_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>
#include "display.h"            /* FRONTBUFFER_ROW_STRIDE_BYTES: the buttons' icons */
#include "https_client.h"
#include "menu_load_json.h"
#include "menu_system.h"
#include "online_account.h"
#include "online_menus.h"
#include "swos2020_menus.h"
#include "text_render.h"        /* GetTextWidth: a name too wide for its column */

enum { ROWS = 18, MAX_PLAYERS = 4096 };
enum { kWhiteText2 = 2, kRedText = 10, kYellowText = 15, kLeftAligned = 1 << 15, kRightAligned = 1 << 14 };

typedef struct {
    char pos[6], name[32], shown[32], nation[4], played[6], won[6], drawn[6], lost[6], srn[7], change[7];
} RankRow;                              /* name in full (the logged-in player's row), shown as it fits */

static int32_t  s_rankId;               /* 5 the Amiga's, 13 the PC's, 8 the XBOX's */
static RankRow *s_rows;
static int32_t  s_count, s_page, s_loading;
static char     s_status[48], s_pageText[32], s_date[24];
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

/* A name too wide for its column ends in a '.': SWOS 2020's shortenName (src/util/util.cpp) with
 * its limit, the column's width less 4 (68 - 18 - 4 there, its 50) -- the small font's glyphs
 * added up (spaces and dots not counted), and the one that goes past it becomes the dot. */
static void shorten_name(char *s)
{
    extern uint8_t _smallCharsTable[];
    const int32_t spacing = ((CharsTableHeader *)_smallCharsTable)->charSpacing;
    const int32_t limit = entry_at(RankingMenu_username_001)->dwWidth - 4;
    int32_t i, w = 0;
    for (i = 0; s[i]; i++) {
        uint8_t one[2] = { (uint8_t)s[i], 0 };
        if (s[i] == ' ' || s[i] == '.')
            continue;
        w += GetTextWidth(one, _smallCharsTable) - spacing;
        if (w > limit) {
            s[i] = '.';
            s[i + 1] = 0;
            break;
        }
    }
}

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
        for (i = 0; v[i] && i < sizeof r->name - 1; i++)    /* (the menu font: capitals) */
            r->name[i] = (char)(v[i] >= 'a' && v[i] <= 'z' ? v[i] - 'a' + 'A' : v[i]);
        r->name[i] = 0;
        memcpy(r->shown, r->name, sizeof r->shown);
        shorten_name(r->shown);
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

/* The new site's flag codes (ISO 3166, England & co. as GB-ENG) as the old site's nation column
 * read: the first three letters of the country's English name. A code not here shows as itself. */
static const char *nation3(const char *code)
{
    static const char *const k[][2] = {
        { "AD", "AND" }, { "AE", "UNI" }, { "AL", "ALB" }, { "AM", "ARM" }, { "AR", "ARG" }, { "AT", "AUS" },
        { "AU", "AUS" }, { "AZ", "AZE" }, { "BA", "BOS" }, { "BD", "BAN" }, { "BE", "BEL" }, { "BG", "BUL" },
        { "BR", "BRA" }, { "BY", "BEL" }, { "CA", "CAN" }, { "CH", "SWI" }, { "CL", "CHI" }, { "CN", "CHI" },
        { "CO", "COL" }, { "CY", "CYP" }, { "CZ", "CZE" }, { "DE", "GER" }, { "DK", "DEN" }, { "DZ", "ALG" },
        { "EE", "EST" }, { "EG", "EGY" }, { "ES", "SPA" }, { "FI", "FIN" }, { "FO", "FAR" }, { "FR", "FRA" },
        { "GB", "UNI" }, { "GB-ENG", "ENG" }, { "GB-NIR", "NIR" }, { "GB-SCT", "SCO" }, { "GB-WLS", "WAL" },
        { "GE", "GEO" }, { "GR", "GRE" }, { "HR", "CRO" }, { "HU", "HUN" }, { "ID", "IND" }, { "IE", "IRE" },
        { "IL", "ISR" }, { "IN", "IND" }, { "IQ", "IRA" }, { "IR", "IRA" }, { "IS", "ICE" }, { "IT", "ITA" },
        { "JO", "JOR" }, { "JP", "JAP" }, { "KR", "KOR" }, { "LB", "LEB" }, { "LT", "LIT" }, { "LU", "LUX" },
        { "LV", "LAT" }, { "MA", "MOR" }, { "MD", "MOL" }, { "ME", "MON" }, { "MK", "MAC" }, { "MT", "MAL" },
        { "MX", "MEX" }, { "NL", "NET" }, { "NO", "NOR" }, { "NZ", "NEW" }, { "PE", "PER" }, { "PL", "POL" },
        { "PT", "POR" }, { "PY", "PAR" }, { "RO", "ROM" }, { "RS", "SER" }, { "RU", "RUS" }, { "SE", "SWE" },
        { "SG", "SIN" }, { "SI", "SLO" }, { "SK", "SLO" }, { "SM", "SAN" }, { "TN", "TUN" }, { "TR", "TUR" },
        { "UA", "UKR" }, { "US", "USA" }, { "UY", "URU" }, { "VA", "VAT" }, { "VE", "VEN" }, { "ZA", "SOU" },
        { "ZM", "ZAM" },
    };
    uint32_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (SDL_strcasecmp(code, k[i][0]) == 0)
            return k[i][1];
    return code;
}

/* {"status":"ok","ranking":{...},"users":[{"rank","name","flag_code","rating","matches_played",
 * "wins","draws","losses",...}]} */
static void parse_json(const char *body, uint32_t len)
{
    UnitedJson all = { body, (int32_t)len }, users, u;
    int32_t pos = 0;
    s_count = 0;
    if (!s_rows)
        s_rows = (RankRow *)malloc(MAX_PLAYERS * sizeof *s_rows);
    if (!s_rows || !United_JsonFind(all, "$.users", &users))
        return;
    while (s_count < MAX_PLAYERS && United_JsonNext(users, &pos, NULL, &u)) {
        RankRow *r = &s_rows[s_count];
        char v[64];
        uint32_t i;
        memset(r, 0, sizeof *r);
        snprintf(r->pos, sizeof r->pos, "%d", (int)(s_count + 1));   /* ("rank" counts the inactive too) */
        United_JsonText(u, "$.name", v, sizeof v);
        for (i = 0; v[i] && i < sizeof r->name - 1; i++)    /* (the menu font: capitals) */
            r->name[i] = (char)(v[i] >= 'a' && v[i] <= 'z' ? v[i] - 'a' + 'A' : v[i]);
        r->name[i] = 0;
        memcpy(r->shown, r->name, sizeof r->shown);
        shorten_name(r->shown);
        United_JsonText(u, "$.flag_code", v, sizeof v);
        snprintf(r->nation, sizeof r->nation, "%s", v[0] ? nation3(v) : "");
        United_JsonText(u, "$.matches_played", r->played, sizeof r->played);
        United_JsonText(u, "$.wins", r->won, sizeof r->won);
        United_JsonText(u, "$.draws", r->drawn, sizeof r->drawn);
        United_JsonText(u, "$.losses", r->lost, sizeof r->lost);
        United_JsonText(u, "$.rating", r->srn, sizeof r->srn);
        s_count++;
    }
}

/* ---- rankingsMenu: AMIGA, PC DOS, XBOX ---- */

static void set_style_icons(void);

static void rankingsMenu_OnInit(void)
{
    set_style_icons();
}

static void rankingsMenu_OnDraw(void)
{
    set_style_icons();
}

static void show_ranking(int32_t id)
{
    s_rankId = id;
    OnlineMenus_Show("swos2020/rankingMenu");
}

static void selectAmigaRankingsMenu(void) { show_ranking(5); }
static void selectPCDOSRankingsMenu(void) { show_ranking(13); }
static void selectXBLARankingsMenu(void)  { show_ranking(8); }

/* The buttons' icons: SWOS 2020's, drawn by United_DrawStyleIcon (src/united_api.c, shared with
 * the CHOOSE GAMEPLAY prompt) 32 in from the button's left and 2 down. Each button draws its own,
 * after itself (afterDrawCb): the cursor leaving a button redraws that button alone, which wiped
 * the icon until the next whole-menu draw -- a blink. */
extern volatile uintptr_t _frontbufferBasePtr;
extern int      _menu_draw_x_offset;

static void style_icon_AfterDraw(MenuEntry *e)
{
    const int32_t icon = e == entry_at(RankingsMenu_amiga) ? 0 : e == entry_at(RankingsMenu_pcDOS) ? 1 : 2;
    United_DrawStyleIcon(icon, e->x + 32, e->y + 2);
}

static void set_style_icons(void)
{
    entry_at(RankingsMenu_amiga)->afterDrawCb = style_icon_AfterDraw;
    entry_at(RankingsMenu_pcDOS)->afterDrawCb = style_icon_AfterDraw;
    entry_at(RankingsMenu_xbla)->afterDrawCb = style_icon_AfterDraw;
}

/* ---- rankingMenu ---- */

/* The page arrows: SWOS 2020's arrows_12x12_13x1_Map (PixelmapAndStrings.h), its up and down
 * (12x12 each, the 11th and 12th), '.' left as it is -- drawScrollButtons, 2 in and 1 down on
 * the up entry, the down one where that entry starts. Each entry draws its own (afterDrawCb),
 * so the cursor leaving one cannot wipe it. */
static const char *const k_pageArrows[12] = {
    "..A...........A.........",
    "..A3..........A3........",
    ".AAA..........A3........",
    ".AAA3.........A3........",
    "AAAAA.........A3........",
    "A.A3A3......A.A3A.......",
    "..A3.3......AAAAA3......",
    "..A3.........AAA33......",
    "..A3.........AAA3.......",
    "..A3..........A33.......",
    "..A3..........A3........",
    "...3...........3........",
};

static void page_arrow_AfterDraw(MenuEntry *e)
{
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    const int32_t down = e == entry_at(RankingMenu_ranking_ScrollDown);
    const int32_t ox = e->x + 2, oy = e->y + (down ? 0 : 1);
    int32_t x, y;
    for (y = 0; y < 12; y++)
        for (x = 0; x < 12; x++) {
            const char c = k_pageArrows[y][down * 12 + x];
            if (c != '.')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

/* The table's columns, wider than SWOS 2020's for the name (50 -> 62) and the nation (18 -> 22),
 * the rest as wide as there and 1 apart: name to +/- is 16..288, centred on 152 as the header
 * and EXIT are (SWOS 2020's ran 14..270, 10 to the left, with room to spare before the arrows).
 * The place stands right of the name, the page and the status move with the table. These are the
 * JSON's coordinates; the loaded menu's are shifted, so each is set relative to EXIT's (107). */
static const struct { int16_t ordinal, x, width; } k_columns[9] = {
    { RankingMenu_pos_000, -6, 21 },      { RankingMenu_username_000, 16, 62 },
    { RankingMenu_nation_000, 79, 22 },   { RankingMenu_played_000, 102, 34 },
    { RankingMenu_wins_000, 137, 32 },    { RankingMenu_draws_000, 170, 32 },
    { RankingMenu_losts_000, 203, 32 },   { RankingMenu_srn_000, 236, 29 },
    { RankingMenu_srnc_000, 266, 22 },
};

static void ranking_layout(void)
{
    const int32_t shift = (int32_t)entry_at(RankingMenu_mexit)->x - 107;
    int32_t c, row;
    for (c = 0; c < 9; c++)
        for (row = 0; row <= ROWS; row++) {
            MenuEntry *e = entry_at(k_columns[c].ordinal + row);
            e->x = (uint16_t)(k_columns[c].x + shift);
            e->dwWidth = (uint16_t)k_columns[c].width;
        }
    entry_at(RankingMenu_ranking_Page)->x = (uint16_t)(266 + shift);
    entry_at(RankingMenu_desc)->x = (uint16_t)(16 + shift);
}

static void rankingMenu_OnInit(void)
{
    time_t now = time(NULL);
    ranking_layout();
    struct tm *t = localtime(&now);
    s_count = s_page = 0;
    snprintf(s_status, sizeof s_status, "LOADING...");
    if (t)
        snprintf(s_date, sizeof s_date, "%02d/%02d/%04d", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900);
    {
        char query[64];
        if (United_NewSite()) {
            snprintf(query, sizeof query, "ranking_id=%d&status=active", (int)s_rankId);
            s_loading = Https_RequestAsync(United_NewApiUrl("rankings/users", query), NULL) == 0;
        } else {
            snprintf(query, sizeof query, "id=%d&format=xml", (int)s_rankId);
            s_loading = Https_RequestAsync(United_ApiUrl("ranking", query), NULL) == 0;
        }
    }
    if (!s_loading)
        snprintf(s_status, sizeof s_status, "BUSY - TRY AGAIN");
}

static void rankingMenu_OnDraw(void)
{
    /* The header row as SWOS 2020's: nothing over the place, name and nation; brown over the rest. */
    static const char *const head[9] = { "", "", "", "PL  ", "W   ", "D   ", "L   ", " SRN ", "+/-" };
    static const int32_t col0[9] = {
        RankingMenu_pos_000, RankingMenu_username_000, RankingMenu_nation_000, RankingMenu_played_000,
        RankingMenu_wins_000, RankingMenu_draws_000, RankingMenu_losts_000, RankingMenu_srn_000,
        RankingMenu_srnc_000,
    };
    const char *me = United_UserName();
    int32_t row, c, pages;

    ranking_layout();                   /* (coming back restores the template) */
    if (s_loading) {
        HttpsReply reply;
        if (Https_PollAsync(&reply)) {
            s_loading = 0;
            if (reply.status == 200 && reply.body) {
                if (United_NewSite())
                    parse_json(reply.body, reply.length);
                else
                    parse(reply.body, reply.length);
                s_status[0] = 0;
                if (!s_count)
                    snprintf(s_status, sizeof s_status, "NO PLAYERS IN THIS RANKING");
            } else {
                snprintf(s_status, sizeof s_status, "CANNOT REACH %s",
                         United_NewSite() ? "SWOS.SWOSUNITED.COM" : "SENSIBLESOCCER.DE");
            }
            Https_Free(&reply);
        }
    }

    entry_at(RankingMenu_header)->fg = (uintptr_t)(s_rankId == 13 ? "PC DOS RANKING" : s_rankId == 8 ? "XBOX RANKING" : "AMIGA RANKING");
    entry_at(RankingMenu_timeStamp)->fg = (uintptr_t)s_date;
    pages = s_count ? (s_count + ROWS - 1) / ROWS : 1;
    if (s_page >= pages)
        s_page = pages - 1;
    snprintf(s_pageText, sizeof s_pageText, "(%d / %d)", (int)s_page + 1, (int)pages);
    entry_at(RankingMenu_ranking_Page)->fg = (uintptr_t)s_pageText;
    entry_at(RankingMenu_desc)->fg = (uintptr_t)s_status;
    entry_at(RankingMenu_ranking_ScrollUp)->afterDrawCb = page_arrow_AfterDraw;
    entry_at(RankingMenu_ranking_ScrollDown)->afterDrawCb = page_arrow_AfterDraw;

    for (c = 0; c < 9; c++) {
        set_cell(0, c, col0[c], head[c], kWhiteText2);
        if (c >= 3) {
            entry_at(col0[c])->background = ENTRY_FRAME_AND_BACK_COLOR;
            entry_at(col0[c])->bgColor = 6;     /* (kLightBrownWithYellowFrame) */
        }
    }
    entry_at(RankingMenu_username_000)->isInvisible = 1;
    entry_at(RankingMenu_nation_000)->isInvisible = 1;
    for (row = 1; row <= ROWS; row++) {
        const int32_t i = s_page * ROWS + row - 1;
        const RankRow *r = i < s_count ? &s_rows[i] : NULL;
        const uint16_t color = r && me[0] && SDL_strcasecmp(r->name, me) == 0 ? kYellowText : kWhiteText2;
        const char *v[9];
        if (r) {
            v[0] = r->pos; v[1] = r->shown; v[2] = r->nation; v[3] = r->played; v[4] = r->won;
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

/* A name: their MATCH INFO (SWOS 2020's ranking_username_onSelect). */
static void ranking_username_onSelect(void)
{
    extern uint8_t _currentMenu[];
    const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
    int32_t r;
    for (r = 0; r < ROWS; r++)
        if (sel == entry_at(RankingMenu_username_001 + r)) {
            const int32_t idx = r + s_page * ROWS;
            if (s_rows && idx < s_count && s_rows[idx].name[0])
                Account_ShowPlayer(s_rows[idx].name);
            return;
        }
}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(rankingsMenu_OnInit),
    HANDLER(rankingsMenu_OnDraw),
    HANDLER(selectAmigaRankingsMenu),
    HANDLER(selectPCDOSRankingsMenu),
    HANDLER(selectXBLARankingsMenu),
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
