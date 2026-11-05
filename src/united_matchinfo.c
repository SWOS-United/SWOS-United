/*
 * united_matchinfo.c -- MATCH INFO: a click on a player's name (the online list, the lobby,
 * RANKINGS). SWOS 2020's menus and its getPlayerAndMatchInfo (src/mpgame.cpp): matchInfoMenu's
 * PLAYER / RANKED MATCH / LEAGUE / TOURNAMENT, playerInfoMenu (id, country, online rankings with
 * their SRN and stars), oooInfoMenu (open ranked matches) and tournaInfoMenu (open league and cup
 * matches, shown by src/united_matches.c, which picks from the same menu).
 *
 *   sensiblesoccer.de   view=userdetail&usr=<name>, then view=matches&uid=<id> -- any player's.
 *   swos.swosunited.com the id from api/rankings/users (its profile_user_id; it is the old site's
 *                       user id too), api/user-details for the rankings and the tournaments
 *                       running, api/tournament's matches for each one's open legs. Open ranked
 *                       matches only exist in api/open-matches, which takes the player's own key,
 *                       so another player's are not known there.
 *
 * SWOS 2020 stops the game while it asks. Here the asking runs on a thread of its own, so the
 * matchmaker and the lobby's link stay alive: MATCH INFO opens at once, says LOADING, and its
 * buttons turn green when the answer is in. A second request makes the first's answer stale
 * (s_gen): it is thrown away when it comes.
 */
#include "online_account.h"
#include "project_identity.h"
#include "united_api.h"

#if PROJECT_ACCOUNTS && defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS) && defined(SWOS_NETPLAY)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "https_client.h"
#include "menu_load_json.h"
#include "menu_system.h"
#include "mm_client.h"
#include "online_menus.h"
#include "swos2020_menus.h"

enum { MI_RANKS = 3, MI_LEGS = 128, MI_OOO = 64, MI_TOURNAS = 8, OOO_ROWS = 8 };
enum { kGreen = 14, kGray = 7, kOrange = 4, kDarkBlue = 3, kRed = 10 };

typedef struct {
    int32_t ok, uid;
    char    country[32];
    int32_t rankCount;
    struct { char label[8]; int32_t srn; } rank[MI_RANKS];
    OpenLeg legs[MI_LEGS];
    int32_t legCount;
    OpenLeg ooo[MI_OOO];
    int32_t oooCount;
} Info;

typedef struct {
    uint32_t gen;
    int32_t  newSite, own, ownId;
    char     name[40], esc[96], key[96];
} Request;

static SDL_mutex *s_lock;
static uint32_t   s_gen;
static int32_t    s_state;              /* 0 asking, 1 s_info is in, -1 failed */
static Info      *s_info;
static char       s_name[40];           /* the player, in capitals */

/* ---- the answer ---- */

/* The rankings played online: the only ones SWOS 2020 shows. */
static const char *online_label(const char *ranking)
{
    if (strcmp(ranking, "AMIGA SWOS WORLD ONLINE") == 0)
        return "AMIGA";
    if (strcmp(ranking, "PC SWOS WORLD ONLINE") == 0)
        return "PC DOS";
    if (strcmp(ranking, "XBOX SWOS WORLD ONLINE") == 0)
        return "XBOX";
    return NULL;
}

static void add_rank(Info *in, const char *ranking, int32_t srn)
{
    const char *label = online_label(ranking);
    if (!label || in->rankCount == MI_RANKS)
        return;
    snprintf(in->rank[in->rankCount].label, sizeof in->rank[0].label, "%s", label);
    in->rank[in->rankCount++].srn = srn;
}

static void set_tourna(OpenLeg *l, const char *name)
{
    char copy[48], *hash;
    uint32_t i;
    snprintf(copy, sizeof copy, "%s", name);
    if ((hash = strstr(copy, " (#")) != NULL)
        *hash = 0;
    for (i = 0; copy[i] && i < sizeof l->tournament - 1; i++)
        l->tournament[i] = (char)(copy[i] >= 'a' && copy[i] <= 'z' ? copy[i] - 'a' + 'A' : copy[i]);
    l->tournament[i] = 0;
}

/* <user id=""><country/><ranking><name/><srn/></ranking>...</user> */
static void parse_user(Info *in, const char *xml, uint32_t len)
{
    const char *end = xml + len, *q = xml, *b, *blockEnd, *id = strstr(xml, " id=\"");
    char *nl;
    in->uid = id ? atoi(id + 5) : 0;
    United_XmlText(xml, end, "country", in->country, sizeof in->country);
    while ((nl = strpbrk(in->country, "\r\n")) != NULL)
        *nl = 0;
    while ((b = United_XmlBlock(q, end, "ranking", &blockEnd)) != NULL) {
        char name[48], v[16];
        United_XmlText(b, blockEnd, "name", name, sizeof name);
        United_XmlText(b, blockEnd, "srn", v, sizeof v);
        add_rank(in, name, atoi(v));
        q = blockEnd;
    }
}

static void xml_leg(OpenLeg *l, const char *b, const char *bEnd)
{
    char v[16];
    United_XmlText(b, bEnd, "match_id", v, sizeof v);
    l->matchId = (uint32_t)strtoul(v, NULL, 10);
    United_XmlText(b, bEnd, "match_leg_id", v, sizeof v);
    l->legId = (uint32_t)strtoul(v, NULL, 10);
    United_XmlText(b, bEnd, "home_user", l->home, sizeof l->home);
    United_XmlText(b, bEnd, "away_user", l->away, sizeof l->away);
}

/* <matchlist><tournament>...<match/></tournament><OOO><match/></OOO></matchlist> */
static void parse_matches(Info *in, const char *xml, uint32_t len)
{
    const char *end = xml + len, *q = xml, *t, *tEnd, *b, *bEnd;
    while ((t = United_XmlBlock(q, end, "tournament", &tEnd)) != NULL) {
        char name[48], ranking[48], v[16];
        const char *m = t;
        United_XmlText(t, tEnd, "name", name, sizeof name);
        United_XmlText(t, tEnd, "ranking", ranking, sizeof ranking);
        United_XmlText(t, tEnd, "rankings_id", v, sizeof v);
        q = tEnd;
        if (!online_label(ranking))
            continue;
        while (in->legCount < MI_LEGS && (b = United_XmlBlock(m, tEnd, "match", &bEnd)) != NULL) {
            OpenLeg *l = &in->legs[in->legCount++];
            xml_leg(l, b, bEnd);
            l->rankingId = atoi(v);
            set_tourna(l, name);
            m = bEnd;
        }
    }
    if ((t = United_XmlBlock(xml, end, "OOO", &tEnd)) != NULL) {
        const char *m = t;
        while (in->oooCount < MI_OOO && (b = United_XmlBlock(m, tEnd, "match", &bEnd)) != NULL) {
            OpenLeg *l = &in->ooo[in->oooCount];
            char v[16];
            xml_leg(l, b, bEnd);
            United_XmlText(b, bEnd, "rankings_id", v, sizeof v);
            l->rankingId = atoi(v);
            in->oooCount++;
            m = bEnd;
        }
    }
}

static int32_t fetch(const char *url, const char *form, HttpsReply *r)
{
    memset(r, 0, sizeof *r);
    if (Https_Request(url, form, r) == 0 && r->status == 200 && r->body)
        return 1;
    Https_Free(r);
    return 0;
}

static void load_old(Info *in, const Request *q)
{
    char url[512], query[160];
    HttpsReply r;

    snprintf(query, sizeof query, "usr=%s&format=xml", q->esc);
    United_ApiUrlTo(url, sizeof url, "userdetail", query);
    if (!fetch(url, NULL, &r))
        return;
    parse_user(in, r.body, r.length);
    Https_Free(&r);
    if (in->uid <= 0)
        return;
    snprintf(query, sizeof query, "uid=%d&format=xml", (int)in->uid);
    United_ApiUrlTo(url, sizeof url, "matches", query);
    in->ok = 1;                         /* (who they are stands, even with their matches not known) */
    if (fetch(url, NULL, &r)) {
        parse_matches(in, r.body, r.length);
        Https_Free(&r);
    }
}

/* The account id of a name: it is in the rankings the player is in. */
static int32_t new_site_id(const Request *q)
{
    static const int32_t ids[3] = { 5, 13, 8 };
    int32_t i;
    if (q->own)
        return q->ownId;
    for (i = 0; i < 3; i++) {
        char url[512], query[32];
        HttpsReply r;
        snprintf(query, sizeof query, "ranking_id=%d", (int)ids[i]);
        United_NewApiUrlTo(url, sizeof url, "rankings/users", query);
        if (fetch(url, NULL, &r)) {
            UnitedJson all = { r.body, (int32_t)r.length }, users, u;
            int32_t pos = 0, found = 0;
            if (United_JsonFind(all, "$.users", &users))
                while (!found && United_JsonNext(users, &pos, NULL, &u)) {
                    char name[40];
                    United_JsonText(u, "$.name", name, sizeof name);
                    if (SDL_strcasecmp(name, q->name) == 0)
                        found = United_JsonInt(u, "$.profile_user_id");
                }
            Https_Free(&r);
            if (found)
                return found;
        }
    }
    return 0;
}

static void new_tournament(Info *in, const Request *q, int32_t id, const char *name, int32_t rankingId)
{
    char url[512], query[64];
    HttpsReply r;
    snprintf(query, sizeof query, "id=%d&include=matches", (int)id);
    United_NewApiUrlTo(url, sizeof url, "tournament", query);
    if (!fetch(url, NULL, &r))
        return;
    {
        UnitedJson all = { r.body, (int32_t)r.length }, list, m;
        int32_t pos = 0;
        if (United_JsonFind(all, "$.matches", &list))
            while (in->legCount < MI_LEGS && United_JsonNext(list, &pos, NULL, &m)) {
                char home[24], away[24], played[8];
                OpenLeg *l;
                United_JsonText(m, "$.home_name", home, sizeof home);
                United_JsonText(m, "$.away_name", away, sizeof away);
                United_JsonText(m, "$.is_played", played, sizeof played);
                if (strcmp(played, "true") == 0 || strcmp(played, "1") == 0 ||
                    (SDL_strcasecmp(home, q->name) != 0 && SDL_strcasecmp(away, q->name) != 0))
                    continue;
                l = &in->legs[in->legCount++];
                l->matchId = (uint32_t)United_JsonInt(m, "$.match_id");
                l->legId = (uint32_t)United_JsonInt(m, "$.leg_id");
                l->rankingId = rankingId;
                snprintf(l->home, sizeof l->home, "%s", home);
                snprintf(l->away, sizeof l->away, "%s", away);
                set_tourna(l, name);
            }
    }
    Https_Free(&r);
}

static void load_new(Info *in, const Request *q)
{
    char url[512], query[80];
    HttpsReply r;

    in->uid = new_site_id(q);
    if (in->uid <= 0)
        return;
    snprintf(query, sizeof query, "user_id=%d&include=rankings,tournaments", (int)in->uid);
    United_NewApiUrlTo(url, sizeof url, "user-details", query);
    if (!fetch(url, NULL, &r))
        return;
    in->ok = 1;
    {
        UnitedJson all = { r.body, (int32_t)r.length }, list, e;
        int32_t pos = 0, n = 0;
        struct { int32_t id, ranking; char name[48]; } t[MI_TOURNAS];
        United_JsonText(all, "$.user.flag_code", in->country, sizeof in->country);
        if (United_JsonFind(all, "$.rankings", &list))
            while (United_JsonNext(list, &pos, NULL, &e)) {
                char name[48];
                United_JsonText(e, "$.ranking_name", name, sizeof name);
                add_rank(in, name, United_JsonInt(e, "$.rating"));
            }
        pos = 0;
        if (United_JsonFind(all, "$.current_tournaments", &list))
            while (n < MI_TOURNAS && United_JsonNext(list, &pos, NULL, &e)) {
                char ranking[48];
                United_JsonText(e, "$.ranking_name", ranking, sizeof ranking);
                if (!online_label(ranking))
                    continue;
                t[n].id = United_JsonInt(e, "$.id");
                t[n].ranking = United_JsonInt(e, "$.ranking_id");
                United_JsonText(e, "$.name", t[n].name, sizeof t[n].name);
                n++;
            }
        Https_Free(&r);
        for (pos = 0; pos < n; pos++)
            new_tournament(in, q, t[pos].id, t[pos].name, t[pos].ranking);
    }
    if (q->own && q->key[0]) {          /* their own ranked matches: only with their key */
        char form[160];
        snprintf(form, sizeof form, "{\"api_key\":\"%s\"}", q->key);
        United_NewApiUrlTo(url, sizeof url, "open-matches", NULL);
        if (fetch(url, form, &r)) {
            UnitedJson all = { r.body, (int32_t)r.length }, list, m;
            int32_t pos = 0;
            if (United_JsonFind(all, "$.ooo_matches", &list))
                while (in->oooCount < MI_OOO && United_JsonNext(list, &pos, NULL, &m)) {
                    OpenLeg *l = &in->ooo[in->oooCount++];
                    l->matchId = (uint32_t)United_JsonInt(m, "$.match_id");
                    l->legId = (uint32_t)United_JsonInt(m, "$.leg_id");
                    l->rankingId = United_JsonInt(m, "$.duel_ranking_id");
                    United_JsonText(m, "$.home_username", l->home, sizeof l->home);
                    United_JsonText(m, "$.away_username", l->away, sizeof l->away);
                }
            Https_Free(&r);
        }
    }
}

static int worker(void *arg)
{
    Request *q = (Request *)arg;
    Info *in = (Info *)calloc(1, sizeof *in);

    if (in) {
        if (q->newSite)
            load_new(in, q);
        else
            load_old(in, q);
    }
    SDL_LockMutex(s_lock);
    if (q->gen == s_gen) {
        s_info = in;
        s_state = in && in->ok ? 1 : -1;
        in = NULL;
    }
    SDL_UnlockMutex(s_lock);
    free(in);
    fprintf(stderr, "[united] match info for '%s' is in\n", q->name);
    free(q);
    return 0;
}

static void ask(const char *name)
{
    Request *q = (Request *)calloc(1, sizeof *q);
    SDL_Thread *t;
    char up[40];
    uint32_t i;

    if (!s_lock)
        s_lock = SDL_CreateMutex();
    for (i = 0; name[i] && i < sizeof up - 1; i++)
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
    up[i] = 0;
    SDL_LockMutex(s_lock);
    s_gen++;
    s_state = 0;
    free(s_info);
    s_info = NULL;
    SDL_UnlockMutex(s_lock);
    snprintf(s_name, sizeof s_name, "%s", up);
    if (!q) {
        s_state = -1;
        return;
    }
    q->gen = s_gen;
    q->newSite = United_NewSite();
    q->own = SDL_strcasecmp(name, United_UserName()) == 0;
    q->ownId = United_UserId();
    snprintf(q->name, sizeof q->name, "%s", name);
    snprintf(q->key, sizeof q->key, "%s", United_ApiKey());
    Https_Escape(up, q->esc, sizeof q->esc);      /* (SWOS 2020 asks in capitals) */
    t = SDL_CreateThread(worker, "matchinfo", q);
    if (t) {
        SDL_DetachThread(t);
    } else {
        free(q);
        s_state = -1;
    }
}

void Account_ShowPlayer(const char *name)
{
    if (!name || !name[0])
        return;
    ask(name);
    OnlineMenus_Show("swos2020/matchInfoMenu");
}

/* ---- the screens ---- */

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static MenuEntry *selected(void)
{
    extern uint8_t _currentMenu[];
    return ((Menu *)_currentMenu)->selectedEntry;
}

/* The matchmaker's connection, while these are up (the lobby's link has its frame hook). */
static void keep_alive(void)
{
    if (MmClient_State() == MM_CONNECTED)
        MmClient_Poll();
}

static void cell(int32_t ordinal, const char *text, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    e->fg = (uintptr_t)text;
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = color;
}

static void link_entry(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

/* SWOS_UNITED_TEST_INFO=<name>[:p|o|t], with --menu=swos2020/matchInfoMenu: asks, and goes on to
 * that screen when the answer is in. */
static const char *s_testInfo;
static char s_testScreen;

static void selectPlayer(void)
{
    if (s_state == 1)
        OnlineMenus_Show("swos2020/playerInfoMenu");
}

static void selectOOO(void)
{
    if (s_state == 1)
        OnlineMenus_Show("swos2020/oooInfoMenu");
}

static void selectTournament(void)
{
    if (s_state == 1)
        UnitedMatches_BrowseTournaments(s_name, s_info->legs, s_info->legCount);
}

static void matchInfoMenu_OnInit(void)
{
    const char *test = getenv("SWOS_UNITED_TEST_INFO");
    if (test && !s_testInfo) {
        static char name[40];
        char *colon;
        snprintf(name, sizeof name, "%s", test);
        if ((colon = strchr(name, ':')) != NULL) {
            s_testScreen = colon[1];
            *colon = 0;
        }
        s_testInfo = name;
        ask(name);
    }
}

static void matchInfoMenu_OnDraw(void)
{
    static const int32_t buttons[3] = { MatchInfoMenu_player, MatchInfoMenu_ooo, MatchInfoMenu_tournament };
    int32_t i;
    keep_alive();
    entry_at(MatchInfoMenu_header)->fg = (uintptr_t)(s_state == 0 ? "LOADING..." : s_state < 0 ? "NO INFO ABOUT THIS PLAYER" : "MATCH INFO");
    for (i = 0; i < 3; i++)
        entry_at(buttons[i])->bgColor = s_state == 1 ? kGreen : kGray;
    if (s_testScreen && s_state == 1) {
        const char which = s_testScreen;
        s_testScreen = 0;
        if (which == 'p')
            selectPlayer();
        else if (which == 'o')
            selectOOO();
        else
            selectTournament();
    }
}

/* PLAYER */
static char s_pimInfo[64];

static void playerInfoMenu_OnInit(void) {}

/* The stars beside each SRN (SWOS 2020's showSRNStarsInPlayerInfoMenu). */
static void pim_stars_AfterDraw(MenuEntry *e)
{
    int32_t i;
    for (i = 0; s_info && i < s_info->rankCount; i++)
        if (e == entry_at(PlayerInfoMenu_srn_01 + i))
            OnlineMenus_DrawStars(s_info->rank[i].srn, e->x + e->dwWidth + 2, e->y + 4);
}

static void playerInfoMenu_OnDraw(void)
{
    char *c;
    int32_t i;

    keep_alive();
    if (!s_info)
        return;
    entry_at(PlayerInfoMenu_header)->fg = (uintptr_t)s_name;
    snprintf(s_pimInfo, sizeof s_pimInfo, "ID: %d, COUNTRY: %s", (int)s_info->uid, s_info->country);
    for (c = s_pimInfo; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 'a' + 'A');
    entry_at(PlayerInfoMenu_info)->fg = (uintptr_t)s_pimInfo;
    cell(PlayerInfoMenu_rankingName_00, "ONLINE RANKING", kOrange);
    cell(PlayerInfoMenu_srn_00, "SRN", kOrange);
    for (i = 0; i < MI_RANKS; i++) {
        static char srn[MI_RANKS][12];
        const int32_t have = i < s_info->rankCount;
        srn[i][0] = 0;
        if (have)
            snprintf(srn[i], sizeof srn[i], "%d", (int)s_info->rank[i].srn);
        cell(PlayerInfoMenu_rankingName_01 + i, have ? s_info->rank[i].label : "", kGray);
        cell(PlayerInfoMenu_srn_01 + i, srn[i], kGray);
        entry_at(PlayerInfoMenu_srn_01 + i)->afterDrawCb = pim_stars_AfterDraw;
    }
}


/* RANKED MATCH */
static int32_t s_oooPage;
static char    s_oooHead[32], s_oooPageText[16], s_oooInfo[32];
static char    s_oooHome[OOO_ROWS][24], s_oooAway[OOO_ROWS][24];

static int32_t ooo_pages(void)
{
    const int32_t n = s_info ? s_info->oooCount : 0;
    return n > 0 ? (n + OOO_ROWS - 1) / OOO_ROWS : 1;
}

static void ooo_arrow_AfterDraw(MenuEntry *e)
{
    United_DrawPageArrow(e == entry_at(OooInfoMenu_scrollDown), e->x + 2, e->y + 1);
}

static void oooInfoMenu_OnDraw(void)
{
    const MenuEntry *sel = selected();
    int32_t r;

    keep_alive();
    if (!s_info)
        return;
    entry_at(OooInfoMenu_header)->fg = (uintptr_t)s_name;
    entry_at(OooInfoMenu_header)->isInvisible = 0;
    entry_at(OooInfoMenu_header2)->isInvisible = 1;
    entry_at(OooInfoMenu_friendlyMatch)->isInvisible = 1;
    snprintf(s_oooHead, sizeof s_oooHead, "RANKED MATCHES (%d)", (int)s_info->oooCount);
    entry_at(OooInfoMenu_subHeader)->fg = (uintptr_t)s_oooHead;
    cell(OooInfoMenu_ranking_00, "RANKING", kOrange);
    cell(OooInfoMenu_home_00, "HOME PLAYER", kOrange);
    cell(OooInfoMenu_away_00, "AWAY PLAYER", kOrange);
    s_oooInfo[0] = 0;
    for (r = 0; r < OOO_ROWS; r++) {
        const int32_t idx = r + s_oooPage * OOO_ROWS;
        const OpenLeg *l = idx < s_info->oooCount ? &s_info->ooo[idx] : NULL;
        const int32_t on = l && (sel == entry_at(OooInfoMenu_ranking_01 + r) || sel == entry_at(OooInfoMenu_home_01 + r) ||
                                 sel == entry_at(OooInfoMenu_away_01 + r));
        char *c;
        s_oooHome[r][0] = s_oooAway[r][0] = 0;
        if (l) {
            snprintf(s_oooHome[r], sizeof s_oooHome[r], "%s", l->home);
            snprintf(s_oooAway[r], sizeof s_oooAway[r], "%s", l->away);
            for (c = s_oooHome[r]; *c; c++)
                if (*c >= 'a' && *c <= 'z')
                    *c = (char)(*c - 'a' + 'A');
            for (c = s_oooAway[r]; *c; c++)
                if (*c >= 'a' && *c <= 'z')
                    *c = (char)(*c - 'a' + 'A');
            if (on)
                snprintf(s_oooInfo, sizeof s_oooInfo, "ID: %u-%u", (unsigned)l->matchId, (unsigned)l->legId);
        }
        cell(OooInfoMenu_ranking_01 + r, !l ? "" : l->rankingId == 13 ? "PC DOS" : l->rankingId == 8 ? "XBOX" : "AMIGA", kGray);
        cell(OooInfoMenu_home_01 + r, s_oooHome[r], on ? kRed : l && strcmp(s_oooHome[r], s_name) != 0 ? kDarkBlue : kGray);
        cell(OooInfoMenu_away_01 + r, s_oooAway[r], on ? kRed : l && strcmp(s_oooAway[r], s_name) != 0 ? kDarkBlue : kGray);
        link_entry(OooInfoMenu_ranking_01 + r, -1, OooInfoMenu_home_01 + r, r ? OooInfoMenu_ranking_01 + r - 1 : -1,
                   r == OOO_ROWS - 1 ? OooInfoMenu_mexit : OooInfoMenu_ranking_01 + r + 1);
        link_entry(OooInfoMenu_home_01 + r, OooInfoMenu_ranking_01 + r, OooInfoMenu_away_01 + r, r ? OooInfoMenu_home_01 + r - 1 : -1,
                   r == OOO_ROWS - 1 ? OooInfoMenu_mexit : OooInfoMenu_home_01 + r + 1);
        link_entry(OooInfoMenu_away_01 + r, OooInfoMenu_home_01 + r, r < OOO_ROWS / 2 - 1 ? OooInfoMenu_scrollUp : OooInfoMenu_scrollDown,
                   r ? OooInfoMenu_away_01 + r - 1 : -1, r == OOO_ROWS - 1 ? OooInfoMenu_mexit : OooInfoMenu_away_01 + r + 1);
    }
    link_entry(OooInfoMenu_scrollUp, OooInfoMenu_away_01, -1, -1, OooInfoMenu_scrollDown);
    link_entry(OooInfoMenu_scrollDown, OooInfoMenu_away_01 + OOO_ROWS - 1, -1, OooInfoMenu_scrollUp, OooInfoMenu_mexit);
    link_entry(OooInfoMenu_mexit, -1, -1, OooInfoMenu_ranking_01 + OOO_ROWS - 1, -1);
    entry_at(OooInfoMenu_scrollUp)->afterDrawCb = entry_at(OooInfoMenu_scrollDown)->afterDrawCb = ooo_arrow_AfterDraw;
    snprintf(s_oooPageText, sizeof s_oooPageText, "(%d / %d)", (int)s_oooPage + 1, (int)ooo_pages());
    entry_at(OooInfoMenu_page)->fg = (uintptr_t)s_oooPageText;
    entry_at(OooInfoMenu_matchInfo)->fg = (uintptr_t)s_oooInfo;
}

static void oooInfo_scrollUp_onSelect(void)     { s_oooPage = (s_oooPage + ooo_pages() - 1) % ooo_pages(); }
static void oooInfo_scrollDown_onSelect(void)   { s_oooPage = (s_oooPage + 1) % ooo_pages(); }
static void oooInfoMenu_HomeUser_OnSelect(void) {}      /* (SWOS 2020's picks a match; here it is to look at) */
static void oooInfoMenu_AwayUser_OnSelect(void) {}
static void oooInfo_friendlyMatch_OnSelect(void) {}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(matchInfoMenu_OnInit),
    HANDLER(matchInfoMenu_OnDraw),
    HANDLER(selectPlayer),
    HANDLER(selectOOO),
    HANDLER(selectTournament),
    HANDLER(playerInfoMenu_OnInit),
    HANDLER(playerInfoMenu_OnDraw),
    HANDLER(oooInfoMenu_OnDraw),
    HANDLER(oooInfo_scrollUp_onSelect),
    HANDLER(oooInfo_scrollDown_onSelect),
    HANDLER(oooInfoMenu_HomeUser_OnSelect),
    HANDLER(oooInfoMenu_AwayUser_OnSelect),
    HANDLER(oooInfo_friendlyMatch_OnSelect),
};
#undef HANDLER

void UnitedMatchInfo_Register(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

#else

void UnitedMatchInfo_Register(void) {}

#endif
