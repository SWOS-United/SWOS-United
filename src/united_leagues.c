/*
 * united_leagues.c -- LEAGUES (SWOS United): SWOS 2020's online-league screens (imported,
 * data_json/menus/swos2020/), with their handlers here. MATCH MAKING's LEAGUES opens ONLINE
 * LEAGUES; INTERNATIONAL COMPETITIONS and DOMESTIC LEAGUES list the thirteen associations; one
 * of them opens its tournaments of this season on sensiblesoccer.de -- a table per group, the
 * results of a group or of a cup round, < > for the association's other tournaments, APPLY
 * its page on the site.
 *
 * SWOS 2020's OnlineLeagueInfo and leagueInfo* / matchView / cupMatchView handlers
 * (src/mpgame.cpp): the season's tournaments from view=season (each with its association,
 * series and id), a tournament from view=tournament&id= -- its rounds, a round's groups (a
 * table and the matches of each) or, in a cup round, its matches.
 *
 * Until the site has a season set up (none on 2026-11-01: view=season answers with an SQL
 * error), every association says "SEASON DATA NOT AVAILABLE FOR NOW"; the tournaments of past
 * seasons are still there. SWOS_UNITED_SEASON_FILE=<file> reads a season from a file in its
 * place, to try the screens.
 *
 * SWOS_UNITED_TEST_LEAGUE=<league>[:<round>[:<group>[:m]]] (with --menu=swos2020/leagueInfoViewMenu)
 * opens that association (0-12, kLeagues' order), goes to that round and group once it has
 * loaded, and with ":m" presses MATCHES -- the screens tried without a mouse.
 *
 * With [mod.menus2020] account_site = swosunited it is swos.swosunited.com's instead (JSON): the season
 * from api/seasons/current (associations, their series, their tournaments), a tournament from
 * api/tournament?id= -- its rounds, the standings of each group of a league round, and every
 * leg played or to play, each with its round and group (0 in a cup round).
 *
 * MY FRIENDLY STATS and MY RANKED STATS were SWOS 2020's own server's (galoreapi): hidden
 * until SWOS United's server keeps its matches (MATCH LOGS).
 */

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)

#include "united_api.h"     /* (SWOS-API's client: third_party/swos-api) */
#include "united_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>
#include "display.h"            /* FRONTBUFFER_ROW_STRIDE_BYTES: the arrows */
#include "https_client.h"
#include "menu_load_json.h"
#include "menu_system.h"
#include "online_menus.h"
#include "platform_sdl.h"       /* Platform_OpenUrl: APPLY */
#include "swos2020_menus.h"

uint16_t GetTextWidth(uint8_t *text, uint8_t *charsTable);     /* (text_render.h: its Round is not ours) */

enum { NUM_LEAGUES = 13, MAX_TOURNA = 32, MAX_ROUNDS = 16, MAX_TABLES = 16,
       MAX_POS = 1024, MAX_MATCHES = 4096, MAX_LINES = 8192,
       TABLE_ROWS = 18, MATCH_ROWS = 18, CUP_ROWS = 14 };
enum { kNoBackground = 0, kLightBrownWithYellowFrame = 6, kGray = 7, kRed = 10, kLightBlue = 13,
       kGreen = 14 };
enum { kWhiteText2 = 2, kGreenText = 14, kLeftAligned = 1 << 15, kRightAligned = 1 << 14 };

/* SWOS 2020's onlineLeagues[]: the site's association names (matched in capitals), and the
 * page APPLY opens. */
static const struct { const char *association, *page; } kLeagues[NUM_LEAGUES] = {
    { "AMIGA (EUROPEAN) SUPER LEAGUE",            "https://sensiblesoccer.de/online-leagues/asl" },
    { "PC LEAGUE",                                "https://sensiblesoccer.de/online-leagues/pc-super-league" },
    { "AMIGA PREMIER LEAGUE",                     "https://sensiblesoccer.de/online-leagues/apl" },
    { "INTERNATIONAL SENSIBLE SOCCER FEDERATION", "https://sensiblesoccer.de/online-leagues/issf-cup" },
    { "EARTH SENSIBLE SOCCER FEDERATION",         "https://sensiblesoccer.de/online-leagues/earthleague" },
    { "CZECH SENSIBLE SOCCER LEAGUE",             "https://sensiblesoccer.de/online-leagues/czech-league" },
    { "DEUTSCHE SENSIBLELIGA",                    "https://sensiblesoccer.de/online-leagues/deutsche-sensible-liga" },
    { "SENSIBLE EKSTRAKLASA POLSKA",              "https://sensiblesoccer.de/online-leagues/ekstraklasa" },
    { "SWOS EREDIVISIE",                          "https://sensiblesoccer.de/online-leagues/swos-eredivisie" },
    { "IBERIAN SENSIBLE LEAGUE",                  "https://sensiblesoccer.de/online-leagues/iberian-league" },
    { "FEDERAZIONE ITALIANA SENSIBLE SOCCER",     "https://sensiblesoccer.de/online-leagues/serie-a" },
    { "TURKISH SENSIBLE SOCCER ASSOCIATION",      "https://sensiblesoccer.de/index.php?option=com_tournament&view=association&id=8" },
    { "UK PREMIER SWOS LEAGUE",                   "https://sensiblesoccer.de/online-leagues/premier-league" },
};
/* (the ISSF's Champions League has a page of its own) */
#define CHAMPIONS_LEAGUE_ID   "6317"
#define CHAMPIONS_LEAGUE_PAGE "https://sensiblesoccer.de/online-leagues/champions-league"

/* ---- the season: each association's tournaments ---- */

typedef struct { char id[12]; } Tourna;
static Tourna  s_tourna[NUM_LEAGUES][MAX_TOURNA];
static int32_t s_numTourna[NUM_LEAGUES], s_curTourna[NUM_LEAGUES];
enum { SEASON_NONE, SEASON_LOADING, SEASON_READY, SEASON_FAILED };
static int32_t s_season;

/* ---- a tournament ---- */

typedef struct { char nr[4], name[24], played[6], wins[6], draws[6], losts[6], goals[6], conceded[6], points[6]; } Pos;
typedef struct {
    char    mid[12], home[24], away[24], hg[4], ag[4], he[4], ae[4], hp[4], ap[4], time[24];
    int32_t finished;
} Match;
typedef struct {
    char    name[32];
    int32_t winners, losers, cup;
    int32_t numTables;                              /* (a group round: a table and a match list each) */
    int32_t posFirst[MAX_TABLES], posCount[MAX_TABLES];
    int32_t matchFirst[MAX_TABLES], matchCount[MAX_TABLES];   /* (a cup round: [0]) */
} Round;

static Pos    *s_pos;
static Match  *s_match;
static Round   s_round[MAX_ROUNDS];
static int32_t s_numPos, s_numMatch, s_numRounds;
static char    s_event[64], s_edition[24], s_name[64], s_tid[12];
enum { TOURNA_NONE, TOURNA_LOADING, TOURNA_READY, TOURNA_FAILED };
static int32_t s_tournaState, s_wantTourna;

/* the screens' place */
static int32_t s_league, s_curRound, s_curTable, s_matchPage, s_cupFirst;

/* the one request in flight: 1 the season, 2 a tournament */
static int32_t s_inflight;

/* ---- text the entries point at ---- */
static char s_cell[TABLE_ROWS + 1][9][28];
static char s_header[96], s_header2[96], s_msg[64], s_group[24], s_page[24];
static char s_mv[MATCH_ROWS][5][28];
static char s_cmv[CUP_ROWS][3][56];
typedef struct { int32_t mode; char t[3][56]; } Line;      /* mode 0: home score away; 1: one text */
static Line   *s_lines;
static int32_t s_numLines;

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 'a' + 'A');
}

static void set_color(int32_t ordinal, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = color;
}

static void set_text_color(int32_t ordinal, const char *text, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    e->fg = (uintptr_t)text;
    e->stringFlags = (uint16_t)((e->stringFlags & ~(uint16_t)0xF) | (color & 0xF));
}

/* ---- the replies ---- */

static void parse_season(const char *xml, uint32_t len)
{
    const char *end = xml + len, *p = xml, *b, *blockEnd;
    int32_t i;
    for (i = 0; i < NUM_LEAGUES; i++)
        s_numTourna[i] = s_curTourna[i] = 0;
    while ((b = United_XmlBlock(p, end, "tournament", &blockEnd)) != NULL) {
        char assoc[96], id[12];
        United_XmlText(b, blockEnd, "association", assoc, sizeof assoc);
        United_XmlText(b, blockEnd, "id", id, sizeof id);
        upper(assoc);
        for (i = 0; i < NUM_LEAGUES; i++)
            if (strcmp(assoc, kLeagues[i].association) == 0 && s_numTourna[i] < MAX_TOURNA)
                snprintf(s_tourna[i][s_numTourna[i]++].id, sizeof s_tourna[0][0].id, "%s", id);
        p = blockEnd;
    }
}

static const char *attr(const char *p, const char *end, const char *name, char *out, uint32_t cap)
{
    char key[32];
    const char *a, *q;
    uint32_t n = 0;
    snprintf(key, sizeof key, " %s=\"", name);
    out[0] = 0;
    a = strstr(p, key);
    if (!a || a >= end)
        return out;
    a += strlen(key);
    for (q = a; q < end && *q != '"' && n + 1 < cap; q++)
        out[n++] = *q;
    out[n] = 0;
    return out;
}

static void parse_matches(const char *p, const char *end, int32_t cupRound, int32_t *first, int32_t *count)
{
    const char *b, *blockEnd;
    *first = s_numMatch;
    *count = 0;
    while (s_numMatch < MAX_MATCHES && (b = United_XmlBlock(p, end, "match", &blockEnd)) != NULL) {
        Match *m = &s_match[s_numMatch];
        char v[8];
        memset(m, 0, sizeof *m);
        United_XmlText(b, blockEnd, "mid", m->mid, sizeof m->mid);
        United_XmlText(b, blockEnd, "home", m->home, sizeof m->home);
        United_XmlText(b, blockEnd, "away", m->away, sizeof m->away);
        United_XmlText(b, blockEnd, "homegoals", m->hg, sizeof m->hg);
        United_XmlText(b, blockEnd, "awaygoals", m->ag, sizeof m->ag);
        if (cupRound) {
            United_XmlText(b, blockEnd, "homeextra", m->he, sizeof m->he);
            United_XmlText(b, blockEnd, "awayextra", m->ae, sizeof m->ae);
            United_XmlText(b, blockEnd, "homepen", m->hp, sizeof m->hp);
            United_XmlText(b, blockEnd, "awaypen", m->ap, sizeof m->ap);
        }
        United_XmlText(b, blockEnd, "finished", v, sizeof v);
        m->finished = atoi(v);
        United_XmlText(b, blockEnd, "finishtime", m->time, sizeof m->time);
        upper(m->home);
        upper(m->away);
        s_numMatch++;
        (*count)++;
        p = blockEnd;
    }
}

static void parse_table(const char *p, const char *end, int32_t *first, int32_t *count)
{
    const char *b, *blockEnd;
    *first = s_numPos;
    *count = 0;
    while (s_numPos < MAX_POS && (b = United_XmlBlock(p, end, "position", &blockEnd)) != NULL) {
        Pos *r = &s_pos[s_numPos];
        United_XmlText(b, blockEnd, "nr", r->nr, sizeof r->nr);
        United_XmlText(b, blockEnd, "username", r->name, sizeof r->name);
        United_XmlText(b, blockEnd, "wins", r->wins, sizeof r->wins);
        United_XmlText(b, blockEnd, "draws", r->draws, sizeof r->draws);
        United_XmlText(b, blockEnd, "losts", r->losts, sizeof r->losts);
        United_XmlText(b, blockEnd, "points", r->points, sizeof r->points);
        United_XmlText(b, blockEnd, "goals", r->goals, sizeof r->goals);
        United_XmlText(b, blockEnd, "conceded", r->conceded, sizeof r->conceded);
        snprintf(r->played, sizeof r->played, "%d", atoi(r->wins) + atoi(r->draws) + atoi(r->losts));
        upper(r->name);
        s_numPos++;
        (*count)++;
        p = blockEnd;
    }
}

/* <tournament event= edition= name= id=><roundlist><round> name, groups (the count), winners,
 * losers; then a group round's <groups> block (<table>s and <matches>, one of each a group)
 * or a cup round's <matches>. */
static void parse_tournament(const char *xml, uint32_t len)
{
    const char *end = xml + len, *p, *b, *blockEnd;
    s_numPos = s_numMatch = s_numRounds = 0;
    s_event[0] = s_edition[0] = s_name[0] = 0;
    if (!s_pos)
        s_pos = (Pos *)malloc(MAX_POS * sizeof *s_pos);
    if (!s_match)
        s_match = (Match *)malloc(MAX_MATCHES * sizeof *s_match);
    if (!s_pos || !s_match)
        return;
    p = strstr(xml, "<tournament ");
    if (p && p < end) {
        const char *tagEnd = strchr(p, '>');
        if (tagEnd && tagEnd < end) {
            attr(p, tagEnd, "event", s_event, sizeof s_event);
            attr(p, tagEnd, "edition", s_edition, sizeof s_edition);
            attr(p, tagEnd, "name", s_name, sizeof s_name);
        }
    }
    p = xml;
    while (s_numRounds < MAX_ROUNDS && (b = United_XmlBlock(p, end, "round", &blockEnd)) != NULL) {
        Round *r = &s_round[s_numRounds];
        const char *countEnd, *g, *gEnd;
        char v[16];
        memset(r, 0, sizeof *r);
        United_XmlText(b, blockEnd, "name", r->name, sizeof r->name);
        upper(r->name);
        United_XmlText(b, blockEnd, "winners", v, sizeof v);
        r->winners = atoi(v);
        United_XmlText(b, blockEnd, "losers", v, sizeof v);
        r->losers = atoi(v);
        countEnd = United_XmlText(b, blockEnd, "groups", v, sizeof v);
        r->cup = atoi(v) == 0;
        if (r->cup) {
            parse_matches(b, blockEnd, 1, &r->matchFirst[0], &r->matchCount[0]);
            r->numTables = 0;
            if (r->matchCount[0])
                s_numRounds++;                  /* (SWOS 2020: a cup round with no matches is left out) */
        } else {
            const char *t, *tEnd, *m, *mEnd, *q;
            g = countEnd ? United_XmlBlock(countEnd, blockEnd, "groups", &gEnd) : NULL;
            if (g) {
                q = g;
                while (r->numTables < MAX_TABLES && (t = United_XmlBlock(q, gEnd, "table", &tEnd)) != NULL) {
                    parse_table(t, tEnd, &r->posFirst[r->numTables], &r->posCount[r->numTables]);
                    if (r->posCount[r->numTables])
                        r->numTables++;
                    q = tEnd;
                }
                q = g;
                {
                    int32_t k = 0;
                    while (k < MAX_TABLES && (m = United_XmlBlock(q, gEnd, "matches", &mEnd)) != NULL) {
                        parse_matches(m, mEnd, 0, &r->matchFirst[k], &r->matchCount[k]);
                        k++;
                        q = mEnd;
                    }
                }
            }
            s_numRounds++;
        }
        p = blockEnd;
    }
    s_curRound = s_curTable = 0;
}

/* ---- the new site's replies (swos.swosunited.com, JSON) ---- */

/* {"season":{"associations":[{"name","series":[{"tournaments":[{"id",...}]}]}]}} */
static void parse_season_json(const char *body, uint32_t len)
{
    UnitedJson all = { body, (int32_t)len }, assocs, a, series, se, tours, t;
    int32_t i, pa = 0;
    for (i = 0; i < NUM_LEAGUES; i++)
        s_numTourna[i] = s_curTourna[i] = 0;
    if (!United_JsonFind(all, "$.season.associations", &assocs))
        return;
    while (United_JsonNext(assocs, &pa, NULL, &a)) {
        char name[96];
        int32_t league = -1, ps = 0;
        United_JsonText(a, "$.name", name, sizeof name);
        upper(name);
        for (i = 0; i < NUM_LEAGUES; i++)
            if (strcmp(name, kLeagues[i].association) == 0)
                league = i;
        if (league < 0 || !United_JsonFind(a, "$.series", &series))
            continue;
        while (United_JsonNext(series, &ps, NULL, &se)) {
            int32_t pt = 0;
            if (!United_JsonFind(se, "$.tournaments", &tours))
                continue;
            while (s_numTourna[league] < MAX_TOURNA && United_JsonNext(tours, &pt, NULL, &t))
                United_JsonText(t, "$.id", s_tourna[league][s_numTourna[league]++].id, sizeof s_tourna[0][0].id);
        }
    }
}

typedef struct { int32_t round, group; } MatchPlace;

/* One leg: {"match_id","round_id","group_id","home_name","away_name","score_a","score_b",
 * "extra_time_score_a/b","penalties_score_a/b","is_played","played_at",...}; the scores are
 * the leg's home side's first, as the old site's were. */
static void json_match(UnitedJson j, Match *m, MatchPlace *at)
{
    char v[8];
    memset(m, 0, sizeof *m);
    United_JsonText(j, "$.match_id", m->mid, sizeof m->mid);
    United_JsonText(j, "$.home_name", m->home, sizeof m->home);
    United_JsonText(j, "$.away_name", m->away, sizeof m->away);
    United_JsonText(j, "$.score_a", m->hg, sizeof m->hg);
    United_JsonText(j, "$.score_b", m->ag, sizeof m->ag);
    United_JsonText(j, "$.extra_time_score_a", m->he, sizeof m->he);
    United_JsonText(j, "$.extra_time_score_b", m->ae, sizeof m->ae);
    United_JsonText(j, "$.penalties_score_a", m->hp, sizeof m->hp);
    United_JsonText(j, "$.penalties_score_b", m->ap, sizeof m->ap);
    United_JsonText(j, "$.is_played", v, sizeof v);
    m->finished = strcmp(v, "true") == 0 || strcmp(v, "1") == 0;
    United_JsonText(j, "$.played_at", m->time, sizeof m->time);
    upper(m->home);
    upper(m->away);
    at->round = United_JsonInt(j, "$.round_id");
    at->group = United_JsonInt(j, "$.group_id");
}

/* The legs of one round (and group: -1 any) from all of them, in the site's order. */
static void take_matches(const Match *all, const MatchPlace *at, int32_t n, int32_t round, int32_t group,
                         int32_t *first, int32_t *count)
{
    int32_t i;
    *first = s_numMatch;
    *count = 0;
    for (i = 0; i < n && s_numMatch < MAX_MATCHES; i++)
        if (at[i].round == round && (group < 0 || at[i].group == group)) {
            s_match[s_numMatch++] = all[i];
            (*count)++;
        }
}

/* A group's standings: [{"name","played","wins","draws","losses","goals_for","goals_against",
 * "points",...}], best first. */
static void json_table(UnitedJson rows, int32_t *first, int32_t *count)
{
    UnitedJson j;
    int32_t pos = 0;
    *first = s_numPos;
    *count = 0;
    while (s_numPos < MAX_POS && United_JsonNext(rows, &pos, NULL, &j)) {
        Pos *r = &s_pos[s_numPos];
        memset(r, 0, sizeof *r);
        snprintf(r->nr, sizeof r->nr, "%d", (int)(*count + 1));
        United_JsonText(j, "$.name", r->name, sizeof r->name);
        United_JsonText(j, "$.played", r->played, sizeof r->played);
        United_JsonText(j, "$.wins", r->wins, sizeof r->wins);
        United_JsonText(j, "$.draws", r->draws, sizeof r->draws);
        United_JsonText(j, "$.losses", r->losts, sizeof r->losts);
        United_JsonText(j, "$.goals_for", r->goals, sizeof r->goals);
        United_JsonText(j, "$.goals_against", r->conceded, sizeof r->conceded);
        United_JsonText(j, "$.points", r->points, sizeof r->points);
        upper(r->name);
        s_numPos++;
        (*count)++;
    }
}

/* {"details":{"name","event_name","edition_name"},"rounds":[{"id","name","is_league",
 * "group_count","qualify_count","losers_total"}],"standings":{"<round>":{"<group>":[...]}},
 * "matches":[...]} */
static void parse_tournament_json(const char *body, uint32_t len)
{
    UnitedJson all = { body, (int32_t)len }, rounds, rj, matches, mj, details;
    Match *legs = NULL;
    MatchPlace *at = NULL;
    int32_t numLegs = 0, pos = 0;
    s_numPos = s_numMatch = s_numRounds = 0;
    s_event[0] = s_edition[0] = s_name[0] = 0;
    if (!s_pos)
        s_pos = (Pos *)malloc(MAX_POS * sizeof *s_pos);
    if (!s_match)
        s_match = (Match *)malloc(MAX_MATCHES * sizeof *s_match);
    legs = (Match *)malloc(MAX_MATCHES * sizeof *legs);
    at = (MatchPlace *)malloc(MAX_MATCHES * sizeof *at);
    if (!s_pos || !s_match || !legs || !at)
        goto done;
    if (United_JsonFind(all, "$.details", &details)) {
        United_JsonText(details, "$.event_name", s_event, sizeof s_event);
        United_JsonText(details, "$.edition_name", s_edition, sizeof s_edition);
        United_JsonText(details, "$.name", s_name, sizeof s_name);
    }
    if (United_JsonFind(all, "$.matches", &matches))
        while (numLegs < MAX_MATCHES && United_JsonNext(matches, &pos, NULL, &mj)) {
            json_match(mj, &legs[numLegs], &at[numLegs]);
            numLegs++;
        }
    pos = 0;
    if (United_JsonFind(all, "$.rounds", &rounds))
        while (s_numRounds < MAX_ROUNDS && United_JsonNext(rounds, &pos, NULL, &rj)) {
            Round *r = &s_round[s_numRounds];
            const int32_t id = United_JsonInt(rj, "$.id");
            char v[8];
            memset(r, 0, sizeof *r);
            United_JsonText(rj, "$.name", r->name, sizeof r->name);
            upper(r->name);
            r->winners = United_JsonInt(rj, "$.qualify_count");
            r->losers = United_JsonInt(rj, "$.losers_total");
            United_JsonText(rj, "$.is_league", v, sizeof v);
            r->cup = !(strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
            if (r->cup) {
                take_matches(legs, at, numLegs, id, -1, &r->matchFirst[0], &r->matchCount[0]);
                if (r->matchCount[0])
                    s_numRounds++;              /* (as the old site's: a cup round with no matches is left out) */
            } else {
                const int32_t groups = United_JsonInt(rj, "$.group_count");
                int32_t g;
                for (g = 1; g <= groups && r->numTables < MAX_TABLES; g++) {
                    char path[48];
                    UnitedJson table;
                    snprintf(path, sizeof path, "$.standings.%d.%d", (int)id, (int)g);
                    if (!United_JsonFind(all, path, &table))
                        continue;
                    json_table(table, &r->posFirst[r->numTables], &r->posCount[r->numTables]);
                    if (!r->posCount[r->numTables])
                        continue;
                    take_matches(legs, at, numLegs, id, g, &r->matchFirst[r->numTables], &r->matchCount[r->numTables]);
                    r->numTables++;
                }
                s_numRounds++;
            }
        }
done:
    free(legs);
    free(at);
    s_curRound = s_curTable = 0;
}

/* ---- requests ---- */

static void request_season(void)
{
    const char *file = getenv("SWOS_UNITED_SEASON_FILE");
    if (s_season == SEASON_LOADING || s_season == SEASON_READY)
        return;
    if (file && file[0]) {
        FILE *f = fopen(file, "rb");
        char *buf = NULL;
        long n = 0;
        if (f && fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 &&
            (buf = (char *)malloc((size_t)n + 1)) != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n) {
            buf[n] = 0;
            if (buf[0] == '{')
                parse_season_json(buf, (uint32_t)n);     /* (the new site's api/seasons/current) */
            else
                parse_season(buf, (uint32_t)n);
            s_season = SEASON_READY;
        } else {
            s_season = SEASON_FAILED;
        }
        free(buf);
        if (f)
            fclose(f);
        return;
    }
    if (s_inflight)
        return;
    if (Https_RequestAsync(United_NewSite() ? United_NewApiUrl("seasons/current", NULL) :
                                              United_ApiUrl("season", "format=xml"), NULL) == 0) {
        s_inflight = 1;
        s_season = SEASON_LOADING;
    }
}

static void request_tournament(void)
{
    char query[96];
    if (s_inflight || s_season != SEASON_READY || s_numTourna[s_league] == 0)
        return;
    snprintf(s_tid, sizeof s_tid, "%s", s_tourna[s_league][s_curTourna[s_league]].id);
    s_name[0] = 0;                              /* (the last one's, until this one answers) */
    snprintf(query, sizeof query, United_NewSite() ? "id=%s&include=details,rounds,matches,standings" : "id=%s&format=xml",
             s_tid);                            /* (the new site: only the sections these screens show) */
    if (Https_RequestAsync(United_NewSite() ? United_NewApiUrl("tournament", query) :
                                              United_ApiUrl("tournament", query), NULL) == 0) {
        s_inflight = 2;
        s_tournaState = TOURNA_LOADING;
        s_wantTourna = 0;
    }
}

/* Every screen's draw: the reply to whatever is in flight, then what is wanted next. */
static void poll(void)
{
    HttpsReply reply;
    if (s_season == SEASON_NONE)
        request_season();                       /* (another screen's request held the line) */
    if (s_inflight && Https_PollAsync(&reply)) {
        const int32_t json = United_NewSite();
        const int32_t ok = reply.status == 200 && reply.body &&
                           (json ? United_JsonOk(reply.body, reply.length) : strstr(reply.body, "<?xml") != NULL);
        if (s_inflight == 1) {
            if (ok && json)
                parse_season_json(reply.body, reply.length);
            else if (ok)
                parse_season(reply.body, reply.length);
            s_season = ok ? SEASON_READY : SEASON_FAILED;
        } else {
            if (ok && json)
                parse_tournament_json(reply.body, reply.length);
            else if (ok)
                parse_tournament(reply.body, reply.length);
            s_tournaState = ok ? TOURNA_READY : TOURNA_FAILED;
        }
        s_inflight = 0;
        Https_Free(&reply);
    }
    if (s_wantTourna)
        request_tournament();
}

static void open_tournament(int32_t dir)
{
    const int32_t n = s_numTourna[s_league];
    if (n == 0)
        return;
    s_curTourna[s_league] = (s_curTourna[s_league] + dir + n) % n;
    s_tournaState = TOURNA_NONE;
    s_wantTourna = 1;
    request_tournament();
}

/* ---- leagueInfoMainMenu: ONLINE LEAGUES ---- */

static void matchMaking_leagues_onSelect(void)
{
    if (s_season == SEASON_FAILED)
        s_season = SEASON_NONE;                  /* (try again) */
    request_season();
    OnlineMenus_Show("swos2020/leagueInfoMainMenu");
}

static void main_stats_hidden(void)
{
    entry_at(LeagueInfoMainMenu_button3)->isInvisible = 1;      /* MY FRIENDLY STATS */
    entry_at(LeagueInfoMainMenu_button4)->isInvisible = 1;      /* MY RANKED STATS */
}

static void leagueInfoMain_onInit(void) { main_stats_hidden(); }
static void leagueInfoMain_onDraw(void) { main_stats_hidden(); poll(); }
static void leagueInfoMain_button1_onSelect(void) { OnlineMenus_Show("swos2020/leagueInfo1Menu"); }
static void leagueInfoMain_button2_onSelect(void) { OnlineMenus_Show("swos2020/leagueInfo2Menu"); }
static void leagueInfoMain_button3_onSelect(void) {}
static void leagueInfoMain_button4_onSelect(void) {}

static void show_league(int32_t idx)
{
    s_league = idx;
    OnlineMenus_Show("swos2020/leagueInfoViewMenu");
}

static void leagueInfo1_onInit(void) {}
static void leagueInfo1_onDraw(void) { poll(); }
static void leagueInfo1_button1_onSelect(void) { show_league(0); }
static void leagueInfo1_button2_onSelect(void) { show_league(1); }
static void leagueInfo1_button3_onSelect(void) { show_league(2); }
static void leagueInfo1_button4_onSelect(void) { show_league(3); }
static void leagueInfo2_onInit(void) {}
static void leagueInfo2_onDraw(void) { poll(); }
static void leagueInfo2_button0_onSelect(void) { show_league(4); }
static void leagueInfo2_button1_onSelect(void) { show_league(5); }
static void leagueInfo2_button2_onSelect(void) { show_league(6); }
static void leagueInfo2_button3_onSelect(void) { show_league(7); }
static void leagueInfo2_button4_onSelect(void) { show_league(8); }
static void leagueInfo2_button5_onSelect(void) { show_league(9); }
static void leagueInfo2_button6_onSelect(void) { show_league(10); }
static void leagueInfo2_button7_onSelect(void) { show_league(11); }
static void leagueInfo2_button8_onSelect(void) { show_league(12); }

/* ---- leagueInfoViewMenu: a tournament's tables ---- */

enum { COLS = 9, COL_STRIDE = TABLE_ROWS + 1 };
static uint16_t s_rowY[COL_STRIDE];

static int32_t cell(int32_t col, int32_t row)
{
    return LeagueInfoViewMenu_pos_000 + col * COL_STRIDE + row;
}

static int32_t s_testRound = -1, s_testTable, s_testMatches;
static void leagueInfoView_matches_onSelect(void);

static void leagueInfoView_onInit(void)
{
    int32_t i;
    const char *test = getenv("SWOS_UNITED_TEST_LEAGUE");
    if (test && test[0] && s_testRound == -1) {
        int league = 0, round = 0, table = 0;
        char m = 0;
        sscanf(test, "%d:%d:%d:%c", &league, &round, &table, &m);
        s_league = league >= 0 && league < NUM_LEAGUES ? league : 0;
        s_testRound = round;
        s_testTable = table;
        s_testMatches = m == 'm';
    }
    for (i = 0; i < COL_STRIDE; i++)
        s_rowY[i] = entry_at(cell(0, i))->y;
    s_tournaState = TOURNA_NONE;
    s_wantTourna = 1;
    if (s_season == SEASON_FAILED)
        s_season = SEASON_NONE;
    request_season();
    request_tournament();
}

/* SWOS 2020's arrows either side of the tournament's name (drawLeagueInfoViewArrows: frames 6 and
 * 2 of arrows_12x12_13x1_Map, the tactics editor's yellow ones), '.' clear, else a palette
 * index in hex. Drawn after the name and after each arrow's own entry (afterDrawCb), so a redraw
 * of any of the three leaves them there. */
static const char *const k_headerArrows[7] = {
    "..F6.............6F.....",
    ".F688.............6F....",
    "2F6655544...4455566F2...",
    "FFF6655548..455566FFF8..",
    ".F68888888...888886F88..",
    "..F6.............6F88...",
    "...88.............88....",
};

static void header_arrows_AfterDraw(MenuEntry *unused)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    const MenuEntry *h = entry_at(LeagueInfoViewMenu_header2);
    const int32_t oy = h->y + (h->dwHeight > 7 ? (h->dwHeight - 7) / 2 : 0);
    const int32_t ox[2] = { h->x - 13, h->x + h->dwWidth + 3 };     /* (3 clear of the name) */
    int32_t side, x, y;
    (void)unused;
    for (side = 0; side < 2; side++)
        for (y = 0; y < 7; y++)
            for (x = 0; x < 12; x++) {
                const char c = k_headerArrows[y][side * 12 + x];
                if (c != '.')
                    fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox[side] + x] =
                        (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
            }
}

/* Too wide for its box: cut and ended with "...", as SWOS 2020's shortenNameEx2 does the name
 * line ("TR SUPER LIG 11/26 - ROUND TR SUPER ...") -- measured in the entry's own font, against
 * its width less a margin (the association's line above: only when it overflows its box). */
static void fit_text(char *s, const MenuEntry *e, int32_t margin)
{
    extern void *_charTables[];
    uint8_t *font = (uint8_t *)_charTables[((e->stringFlags & STR_FLAGS_MASK) >> 2) / 4];
    const int32_t limit = (int32_t)e->dwWidth - margin;
    char buf[128];
    int32_t n = (int32_t)strlen(s);
    if (!font || (int32_t)GetTextWidth((uint8_t *)s, font) <= limit)
        return;
    while (n > 0) {
        snprintf(buf, sizeof buf, "%.*s...", (int)--n, s);
        if ((int32_t)GetTextWidth((uint8_t *)buf, font) <= limit)
            break;
    }
    strcpy(s, buf);
}

static void leagueInfoView_header2_dec_onSelect(void) { open_tournament(-1); }
static void leagueInfoView_header2_onSelect(void)     { open_tournament(0); }
static void leagueInfoView_header2_inc_onSelect(void) { open_tournament(+1); }

static int32_t view_ready(void)
{
    return s_tournaState == TOURNA_READY && s_numRounds > 0;
}

static void leagueInfoView_onDraw(void)
{
    static const char *const head[COLS] = { "", "", "PL ", "W  ", "D  ", "L  ", "F  ", "A  ", "PTS" };
    const Round *r;
    int32_t row, c, size = 0, roundButton = 0, tableButton = 0, gapY;

    poll();
    if (s_testRound >= 0 && view_ready()) {     /* (SWOS_UNITED_TEST_LEAGUE) */
        if (s_testRound < s_numRounds)
            s_curRound = s_testRound;
        if (s_testTable < s_round[s_curRound].numTables)
            s_curTable = s_testTable;
        s_testRound = -2;
        if (s_testMatches)
            leagueInfoView_matches_onSelect();
    }

    /* SWOS 2020's yellow arrows around the tournament's name (the arrows' entries stay, clear, to
     * be clicked), and left / right on the name itself go through the association's tournaments
     * as the arrows do (SWOS 2020's finalControlsStatus 6 / 2) -- FIRE on it reloads this one. */
    {
        extern uint8_t  _currentMenu[];
        extern uint16_t _menuControlsDirection;
        MenuEntry *dec = entry_at(LeagueInfoViewMenu_header2_dec), *inc = entry_at(LeagueInfoViewMenu_header2_inc);
        MenuEntry *name = entry_at(LeagueInfoViewMenu_header2);
        dec->background = inc->background = 0;
        dec->fg = inc->fg = (uintptr_t)" ";
        dec->afterDrawCb = inc->afterDrawCb = name->afterDrawCb = header_arrows_AfterDraw;
        if (((Menu *)_currentMenu)->selectedEntry == name) {
            if (_menuControlsDirection == 6)
                open_tournament(-1);
            else if (_menuControlsDirection == 2)
                open_tournament(+1);
        }
    }

    /* what to say while there is no table */
    s_msg[0] = 0;
    if (s_season == SEASON_LOADING || s_season == SEASON_NONE)
        snprintf(s_msg, sizeof s_msg, "LOADING...");
    else if (s_season == SEASON_FAILED || s_numTourna[s_league] == 0)
        snprintf(s_msg, sizeof s_msg, "SEASON DATA NOT AVAILABLE FOR NOW");
    else if (s_tournaState == TOURNA_LOADING || s_tournaState == TOURNA_NONE)
        snprintf(s_msg, sizeof s_msg, "LOADING...");
    else if (s_tournaState == TOURNA_FAILED)
        snprintf(s_msg, sizeof s_msg, "CANNOT REACH SENSIBLESOCCER.DE");
    else if (s_numRounds == 0)
        snprintf(s_msg, sizeof s_msg, "NO MATCHES IN THIS TOURNAMENT YET");

    r = view_ready() ? &s_round[s_curRound] : NULL;
    if (r && r->cup)
        snprintf(s_msg, sizeof s_msg, "CLICK MATCHES BUTTON TO VIEW MATCH RESULTS");
    entry_at(LeagueInfoViewMenu_msg)->fg = (uintptr_t)s_msg;
    entry_at(LeagueInfoViewMenu_msg)->isInvisible = s_msg[0] ? 0 : 1;

    /* the buttons: green when they do something, grey when not */
    set_color(LeagueInfoViewMenu_matches, r && (r->cup ? r->matchCount[0] > 0 : r->numTables > 0) ? kGreen : kGray);
    tableButton = r && !r->cup && r->numTables > 1;
    roundButton = r && s_numRounds > 1;
    set_color(LeagueInfoViewMenu_nextTable, tableButton ? kGreen : kGray);
    set_color(LeagueInfoViewMenu_nextRound, roundButton ? kGreen : kGray);
    set_color(LeagueInfoViewMenu_apply, kGreen);

    /* the headers */
    if (r) {
        snprintf(s_header, sizeof s_header, "%s (%s)", s_event, s_edition);
        if (roundButton)
            snprintf(s_header2, sizeof s_header2, "%s - ROUND %s", s_name, r->name);
        else
            snprintf(s_header2, sizeof s_header2, "%s", s_name);
    } else {
        snprintf(s_header, sizeof s_header, "%s", kLeagues[s_league].association);
        /* the tournament's name only when it is this association's: another one's (the last
         * loaded, before this association had any) must not stand under this one's name */
        const int32_t ours = s_numTourna[s_league] > 0 &&
                             strcmp(s_tid, s_tourna[s_league][s_curTourna[s_league]].id) == 0;
        snprintf(s_header2, sizeof s_header2, "%s", s_tournaState == TOURNA_LOADING || !ours ? "" : s_name);
    }
    upper(s_header);
    upper(s_header2);
    fit_text(s_header, entry_at(LeagueInfoViewMenu_header), 0);      /* (only when it overflows) */
    fit_text(s_header2, entry_at(LeagueInfoViewMenu_header2), 6);
    entry_at(LeagueInfoViewMenu_header)->fg = (uintptr_t)s_header;
    entry_at(LeagueInfoViewMenu_header2)->fg = (uintptr_t)s_header2;

    /* the table */
    if (r && !r->cup && r->numTables > 0) {
        size = r->posCount[s_curTable];
        if (size > TABLE_ROWS)
            size = TABLE_ROWS;
    }
    for (c = 0; c < COLS; c++) {
        snprintf(s_cell[0][c], sizeof s_cell[0][c], "%s", size ? head[c] : "");
        set_text_color(cell(c, 0), s_cell[0][c], kWhiteText2);
        set_color(cell(c, 0), size && c >= 2 ? kLightBrownWithYellowFrame : kNoBackground);
    }
    if (size && tableButton) {                  /* GROUP A, B, ... over the names */
        snprintf(s_group, sizeof s_group, "         GROUP %c", 'A' + s_curTable);
        set_text_color(cell(1, 0), s_group, kWhiteText2);
        set_color(cell(1, 0), kLightBrownWithYellowFrame);
    }
    for (row = 1; row <= TABLE_ROWS; row++) {
        const Pos *p = row <= size ? &s_pos[r->posFirst[s_curTable] + row - 1] : NULL;
        const char *v[COLS];
        if (p) {
            v[0] = p->nr; v[1] = p->name; v[2] = p->played; v[3] = p->wins; v[4] = p->draws;
            v[5] = p->losts; v[6] = p->goals; v[7] = p->conceded; v[8] = p->points;
        }
        for (c = 0; c < COLS; c++) {
            snprintf(s_cell[row][c], sizeof s_cell[row][c], "%s", p ? v[c] : "");
            set_text_color(cell(c, row), s_cell[row][c], kWhiteText2);
            /* SWOS 2020's setEntryLV: no background for the place, red for the name, grey for the rest */
            set_color(cell(c, row), !p || c == 0 ? kNoBackground : c == 1 ? kRed : kGray);
        }
    }
    /* who goes up (blue) and who goes down (grey), on a round's only table */
    if (size && !roundButton && !tableButton) {
        const int32_t full = r->posCount[s_curTable];
        for (row = 0; row < r->winners && row < size; row++)
            set_color(cell(1, row + 1), kLightBlue);
        for (row = full - r->losers; row < full; row++)
            if (row >= 0 && row < size)
                for (c = 1; c < COLS; c++)
                    set_color(cell(c, row + 1), kGray);
    }
    /* a short table in the middle of the screen */
    gapY = (8 * (COL_STRIDE - (size + 1))) / 2;
    for (row = 0; row < COL_STRIDE; row++)
        for (c = 0; c < COLS; c++)
            entry_at(cell(c, row))->y = (uint16_t)(s_rowY[row] + gapY);
}

static void leagueInfoView_apply_onSelect(void)
{
    const char *url = kLeagues[s_league].page;
    if (s_league == 3 && strcmp(s_tid, CHAMPIONS_LEAGUE_ID) == 0)
        url = CHAMPIONS_LEAGUE_PAGE;
    Platform_OpenUrl(url);              /* (says so in the log when it cannot) */
}

/* A cup round's results, as SWOS 2020's makeResultInfo writes them: each tie's legs, then who
 * won it (in extra time, after penalties) or "V" for a tie not played yet. */
static void add_line(int32_t mode, const char *a, const char *b, const char *c)
{
    Line *l;
    if (!s_lines)
        s_lines = (Line *)malloc(MAX_LINES * sizeof *s_lines);
    if (!s_lines || s_numLines >= MAX_LINES)
        return;
    l = &s_lines[s_numLines++];
    l->mode = mode;
    snprintf(l->t[0], sizeof l->t[0], "%s", a);
    snprintf(l->t[1], sizeof l->t[1], "%s", b ? b : "");
    snprintf(l->t[2], sizeof l->t[2], "%s", c ? c : "");
    upper(l->t[0]);
    upper(l->t[1]);
    upper(l->t[2]);
}

static void make_cup_lines(const Round *r)
{
    const Match *ms = &s_match[r->matchFirst[0]];
    const int32_t n = r->matchCount[0];
    int32_t i = 0;
    s_numLines = 0;
    while (i < n) {
        int32_t j = i, leg = 0, goals[2] = { 0, 0 }, extra[2] = { 0, 0 }, pen[2] = { 0, 0 }, any;
        char text[96], score[16];
        while (j < n && strcmp(ms[j].mid, ms[i].mid) == 0) {     /* a tie's legs, home first */
            const Match *m = &ms[j];
            const int32_t f = leg % 2 == 0 ? 0 : 1;              /* (the second leg is played the other way round) */
            goals[f] += atoi(m->hg); goals[!f] += atoi(m->ag);
            extra[f] += atoi(m->he); extra[!f] += atoi(m->ae);
            pen[f] += atoi(m->hp); pen[!f] += atoi(m->ap);
            leg++;
            j++;
        }
        any = goals[0] + goals[1] + extra[0] + extra[1] + pen[0] + pen[1];
        if (!any) {
            add_line(0, ms[i].home, "V", ms[i].away);
        } else {
            int32_t k;
            for (k = i; k < j; k++) {
                snprintf(score, sizeof score, "%s-%s", ms[k].hg, ms[k].ag);
                add_line(0, ms[k].home, score, ms[k].away);
            }
            extra[0] += goals[0];
            extra[1] += goals[1];
            if (goals[0] != goals[1]) {
                const int32_t w = goals[0] > goals[1] ? 0 : 1;
                snprintf(text, sizeof text, "%s wins %d-%d", w ? ms[i].away : ms[i].home, (int)goals[w], (int)goals[!w]);
                add_line(1, text, NULL, NULL);
            } else {
                snprintf(text, sizeof text, "Draws %d-%d", (int)goals[0], (int)goals[1]);
                add_line(1, text, NULL, NULL);
                if (extra[0] != extra[1]) {
                    const int32_t w = extra[0] > extra[1] ? 0 : 1;
                    snprintf(text, sizeof text, "%s wins %d-%d in Extra Time", w ? ms[i].away : ms[i].home,
                             (int)extra[w], (int)extra[!w]);
                    add_line(1, text, NULL, NULL);
                } else {
                    const int32_t w = pen[0] > pen[1] ? 0 : 1;
                    snprintf(text, sizeof text, "%d-%d in Extra Time", (int)extra[0], (int)extra[1]);
                    add_line(1, text, NULL, NULL);
                    snprintf(text, sizeof text, "%s wins %d-%d after Penalties", w ? ms[i].away : ms[i].home,
                             (int)pen[w], (int)pen[!w]);
                    add_line(1, text, NULL, NULL);
                }
            }
        }
        add_line(1, "", NULL, NULL);
        i = j;
    }
}

static void leagueInfoView_matches_onSelect(void)
{
    const Round *r;
    if (!view_ready())
        return;
    r = &s_round[s_curRound];
    if (!r->cup) {
        if (r->numTables > 0) {
            s_matchPage = 0;
            OnlineMenus_Show("swos2020/matchViewMenu");
        }
    } else if (r->matchCount[0] > 0) {
        make_cup_lines(r);
        s_cupFirst = 0;
        OnlineMenus_Show("swos2020/cupMatchViewMenu");
    }
}

static void leagueInfoView_nextTable_onSelect(void)
{
    const Round *r;
    if (!view_ready())
        return;
    r = &s_round[s_curRound];
    if (!r->cup && r->numTables > 1)
        s_curTable = (s_curTable + 1) % r->numTables;
}

static void leagueInfoView_nextRound_onSelect(void)
{
    if (!view_ready() || s_numRounds < 2)
        return;
    s_curRound = (s_curRound + 1) % s_numRounds;
    s_curTable = 0;
}

/* ---- matchViewMenu: a group's matches, 18 a page ---- */

static void scroll_buttons(int32_t up, int32_t down)
{
    MenuEntry *u = entry_at(up), *d = entry_at(down);
    u->background = d->background = ENTRY_FRAME_AND_BACK_COLOR;
    u->bgColor = d->bgColor = kRed;
    u->fg = (uintptr_t)"-";
    d->fg = (uintptr_t)"+";
}

static int32_t match_pages(void)
{
    const Round *r = &s_round[s_curRound];
    const int32_t n = s_curTable < MAX_TABLES ? r->matchCount[s_curTable] : 0;
    return n ? (n + MATCH_ROWS - 1) / MATCH_ROWS : 1;
}

static void matchView_onInit(void) { s_matchPage = 0; }
static void matchView_onReturn(void) {}

static void matchView_onDraw(void)
{
    static const char *const head[5] = { " HOME PLAYER", " AWAY PLAYER", "", "", "      DATE PLAYED" };
    const Round *r = &s_round[s_curRound];
    const Match *ms = &s_match[r->matchFirst[s_curTable]];
    const int32_t n = r->matchCount[s_curTable];
    int32_t row, c;

    for (c = 0; c < 5; c++) {
        set_text_color(MatchViewMenu_home_000 + c * COL_STRIDE, head[c], kWhiteText2);
        set_color(MatchViewMenu_home_000 + c * COL_STRIDE, kLightBrownWithYellowFrame);
    }
    for (row = 0; row < MATCH_ROWS; row++) {
        const int32_t i = s_matchPage * MATCH_ROWS + row;
        const Match *m = i < n ? &ms[i] : NULL;
        snprintf(s_mv[row][0], sizeof s_mv[row][0], "%s", m ? m->home : "");
        snprintf(s_mv[row][1], sizeof s_mv[row][1], "%s", m ? m->away : "");
        snprintf(s_mv[row][2], sizeof s_mv[row][2], "%s", m && m->finished ? m->hg : "");
        snprintf(s_mv[row][3], sizeof s_mv[row][3], "%s", m && m->finished ? m->ag : "");
        snprintf(s_mv[row][4], sizeof s_mv[row][4], "%s", m && m->finished ? m->time : "");
        for (c = 0; c < 5; c++)
            set_text_color(MatchViewMenu_home_000 + c * COL_STRIDE + row + 1, s_mv[row][c], kWhiteText2);
    }
    snprintf(s_page, sizeof s_page, "(%d / %d)", (int)s_matchPage + 1, (int)match_pages());
    entry_at(MatchViewMenu_page)->fg = (uintptr_t)s_page;
    scroll_buttons(MatchViewMenu_scrollUp, MatchViewMenu_scrollDown);
}

static void matchView_scrollUp_onSelect(void)
{
    const int32_t pages = match_pages();
    s_matchPage = (s_matchPage + pages - 1) % pages;
}

static void matchView_scrollDown_onSelect(void)
{
    s_matchPage = (s_matchPage + 1) % match_pages();
}

/* ---- cupMatchViewMenu: a cup round's ties, 14 lines at a time ---- */

static void cupMatchView_onInit(void) { s_cupFirst = 0; }
static void cupMatchView_onReturn(void) {}

static void cupMatchView_onDraw(void)
{
    int32_t row;
    for (row = 0; row < CUP_ROWS; row++) {
        const int32_t i = s_cupFirst + row;
        const Line *l = i < s_numLines ? &s_lines[i] : NULL;
        uint16_t color;
        if (l && l->mode == 0) {
            snprintf(s_cmv[row][0], sizeof s_cmv[row][0], "%s", l->t[0]);
            snprintf(s_cmv[row][1], sizeof s_cmv[row][1], "%s", l->t[1]);
            snprintf(s_cmv[row][2], sizeof s_cmv[row][2], "%s", l->t[2]);
        } else {
            s_cmv[row][0][0] = s_cmv[row][2][0] = 0;
            snprintf(s_cmv[row][1], sizeof s_cmv[row][1], "%s", l ? l->t[0] : "");
        }
        color = s_cmv[row][0][0] ? kWhiteText2 : kGreenText;
        set_text_color(CupMatchViewMenu_topleft_000 + row, s_cmv[row][0], color);
        set_text_color(CupMatchViewMenu_topcenter_000 + row, s_cmv[row][1], color);
        set_text_color(CupMatchViewMenu_topright_000 + row, s_cmv[row][2], color);
    }
    scroll_buttons(CupMatchViewMenu_scrollUp, CupMatchViewMenu_scrollDown);
}

static void cupMatchView_scrollUp_onSelect(void)
{
    if (s_cupFirst > 0)
        s_cupFirst--;
}

static void cupMatchView_scrollDown_onSelect(void)
{
    if (s_cupFirst + CUP_ROWS < s_numLines)
        s_cupFirst++;
}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(matchMaking_leagues_onSelect),
    HANDLER(leagueInfoMain_onInit),
    HANDLER(leagueInfoMain_onDraw),
    HANDLER(leagueInfoMain_button1_onSelect),
    HANDLER(leagueInfoMain_button2_onSelect),
    HANDLER(leagueInfoMain_button3_onSelect),
    HANDLER(leagueInfoMain_button4_onSelect),
    { "leagueInfoMain_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
    HANDLER(leagueInfo1_onInit),
    HANDLER(leagueInfo1_onDraw),
    HANDLER(leagueInfo1_button1_onSelect),
    HANDLER(leagueInfo1_button2_onSelect),
    HANDLER(leagueInfo1_button3_onSelect),
    HANDLER(leagueInfo1_button4_onSelect),
    { "leagueInfo1_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
    HANDLER(leagueInfo2_onInit),
    HANDLER(leagueInfo2_onDraw),
    HANDLER(leagueInfo2_button0_onSelect),
    HANDLER(leagueInfo2_button1_onSelect),
    HANDLER(leagueInfo2_button2_onSelect),
    HANDLER(leagueInfo2_button3_onSelect),
    HANDLER(leagueInfo2_button4_onSelect),
    HANDLER(leagueInfo2_button5_onSelect),
    HANDLER(leagueInfo2_button6_onSelect),
    HANDLER(leagueInfo2_button7_onSelect),
    HANDLER(leagueInfo2_button8_onSelect),
    { "leagueInfo2_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
    HANDLER(leagueInfoView_onInit),
    HANDLER(leagueInfoView_onDraw),
    HANDLER(leagueInfoView_header2_dec_onSelect),
    HANDLER(leagueInfoView_header2_onSelect),
    HANDLER(leagueInfoView_header2_inc_onSelect),
    HANDLER(leagueInfoView_apply_onSelect),
    HANDLER(leagueInfoView_matches_onSelect),
    HANDLER(leagueInfoView_nextTable_onSelect),
    HANDLER(leagueInfoView_nextRound_onSelect),
    { "leagueInfoView_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
    HANDLER(matchView_onInit),
    HANDLER(matchView_onDraw),
    HANDLER(matchView_onReturn),
    HANDLER(matchView_scrollUp_onSelect),
    HANDLER(matchView_scrollDown_onSelect),
    { "matchView_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
    HANDLER(cupMatchView_onInit),
    HANDLER(cupMatchView_onDraw),
    HANDLER(cupMatchView_onReturn),
    HANDLER(cupMatchView_scrollUp_onSelect),
    HANDLER(cupMatchView_scrollDown_onSelect),
    { "cupMatchView_exit_onSelect", (void *)SetExitMenuFlag, SYM_FUNC },
};
#undef HANDLER

void UnitedLeagues_Register(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

#else

void UnitedLeagues_Register(void) {}

#endif
