/*
 * united_matches.c -- ranked and league / cup matches (SWOS United): the service side of
 * OpenSWOS's lobby hooks (include/online_account.h), ported from SWOS 2020's mpgame.cpp.
 *
 *   OPEN       SWOS 2020's openLobbyMenu: FRIENDLY, RANKED MATCH, LEAGUE / CUP. LEAGUE / CUP
 *              looks for the player's open league and cup matches (view=matches&uid=); with
 *              none, the same screen says so and offers ONLINE LEAGUES and JOIN DISCORD TO APPLY.
 *   THE LOBBY  both players' SRN for the game's style (view=userdetail: the Amiga ranking 5, the
 *              PC's 13) as the bot's lines; a ranked match the two ratings do not allow --
 *              login_check.php's [SRN_DIFF_LIMIT], 750 and 1200 -- becomes a friendly (the
 *              host's rules). SELECT MATCH (league / cup, the host's): the first open leg
 *              between the two of them in a tournament of the game's style.
 *   RESULTS    each player sends their OWN, with their own account: view=matchleginsert,
 *              lid, pg/og their goals and the opponent's in the ninety minutes, pe/oe extra
 *              time, pp/op penalties -- POSTed, so the password stays out of the site's logs. A
 *              ranked match is first created on the site by the host (view=matchcreate,
 *              createOOO, or createOOOleg for the next match between the same two), whose ids go
 *              to the joiner in the lobby's rules. (SWOS 2020 sent the joiner's password to the
 *              host so the host could submit both. Not here.)
 *
 * With [netplay] account_site = swosunited a RANKED match is swos.swosunited.com's (SWOS-API
 * issue 4, JSON with the log-in's api_key): the SRNs from api/rankings/users (both rankings,
 * every player: the lobby knows the other player by name, not id); the host makes the session
 * after the first match (swos-matches/create, ranking + opponent's name) and a leg after each
 * later one (add-leg), and the ids go to the joiner in the rules as before; each player reports
 * their own score (swos-matches/report: waiting until both have, then applied; conflict when the
 * two differ). Elo counts a session only once it is CLOSED, and closing deletes every leg not
 * yet applied -- so a player closes it on leaving the lobby only when their last report was the
 * one that applied it (both scores in). LEAGUE / CUP there (issue 5): the open legs from
 * api/open-matches ("league_matches", each with its duel_ranking_id), reported the same way;
 * the tournament system makes and closes those, and books their Elo.
 */
#include "united_api.h"
#include "online_account.h"
#include "project_identity.h"

#if PROJECT_ACCOUNTS && defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS) && defined(SWOS_NETPLAY)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "dos_graphics.h"
#include "https_client.h"
#include "menu_load_json.h"
#include "menu_system.h"
#include "netplay_lobby.h"
#include "online_menus.h"
#include "swos2020_menus.h"

#if defined(__has_include)
#if __has_include("united_secrets.h")
#include "united_secrets.h"
#endif
#endif
#ifndef UNITED_RANKED_CREATE_KEY
#define UNITED_RANKED_CREATE_KEY ""
#define UNITED_RANKED_ADDLEG_KEY ""
#endif

enum { UNRANKED = -5000, MAX_LEGS = 96, MAX_JOBS = 8 };

/* ---- the requests: one at a time, in order ---- */

typedef enum { JOB_LIMITS, JOB_MY_DETAIL, JOB_PEER_DETAIL, JOB_MY_MATCHES, JOB_CREATE, JOB_SUBMIT,
               /* the new site's (swos.swosunited.com) */
               JOB_RANKING, JOB_N_CREATE, JOB_N_ADDLEG, JOB_N_REPORT, JOB_N_CLOSE, JOB_N_OPEN } JobKind;
typedef struct {
    JobKind  kind;
    char     url[384];
    char     form[384];
    uint32_t legId;                     /* (JOB_RANKING: the ranking's id) */
} Job;

static Job     s_jobs[MAX_JOBS];
static int32_t s_jobCount, s_running;
static Job     s_current;

static void job_add(JobKind kind, const char *url, const char *form, uint32_t legId)
{
    Job *j;
    if (s_jobCount == MAX_JOBS)
        return;
    j = &s_jobs[s_jobCount++];
    j->kind = kind;
    snprintf(j->url, sizeof j->url, "%s", url);
    snprintf(j->form, sizeof j->form, "%s", form ? form : "");
    j->legId = legId;
}

/* ---- what the site said ---- */

typedef struct {
    int32_t have, failed, uid, srnAmiga, srnPC;
    char    name[24];
} Player;

typedef struct {
    uint32_t matchId, legId;
    int32_t  rankingId;
    char     tournament[32], home[24], away[24];
} OpenLeg;

static Player   s_me, s_peer;
static int32_t  s_limit = 750, s_limit2 = 1200, s_limitsLoaded;
static OpenLeg  s_legs[MAX_LEGS];
static int32_t  s_legCount, s_legsLoaded;
static uint32_t s_done[32];             /* legs this session submitted */
static int32_t  s_doneCount;
static int32_t  s_host, s_srnTold, s_testSelected;

/* A ranked match: the host's, between these two players in this lobby */
static uint32_t s_rankedMatchId;
static int32_t  s_haveRules;            /* host: s_rulesMatch / s_rulesLeg to put in the rules */
static uint32_t s_rulesMatch, s_rulesLeg;
static AccountResult s_guestResult;     /* joiner: a ranked result waiting for the host's leg id */
static int32_t  s_guestWaiting;
static uint32_t s_guestLegBefore;
static AccountResult s_hostResult;      /* host: its own ranked result, waiting for the leg */
static int32_t  s_hostPending;
/* the new site: the session this lobby's ranked matches are in, and whether this player's last
 * report was the one that applied its leg (then this player closes the session on leaving) */
static uint32_t s_sessionId;
static int32_t  s_lastApplied, s_rankingsAsked;

static int32_t srn_for_style(const Player *p, int32_t dos)
{
    const int32_t v = dos ? p->srnPC : p->srnAmiga;
    return v == UNRANKED ? 1000 : v;    /* (SWOS 2020: an unranked player counts as 1000) */
}

static void parse_user(const char *xml, uint32_t len, Player *p)
{
    const char *end = xml + len, *q = xml, *b, *blockEnd;
    const char *id = strstr(xml, " id=\"");
    p->uid = id ? atoi(id + 5) : 0;
    p->srnAmiga = p->srnPC = UNRANKED;
    while ((b = United_XmlBlock(q, end, "ranking", &blockEnd)) != NULL) {
        char v[16];
        int32_t rid;
        United_XmlText(b, blockEnd, "id", v, sizeof v);
        rid = atoi(v);
        United_XmlText(b, blockEnd, "srn", v, sizeof v);
        if (rid == 5)
            p->srnAmiga = atoi(v);
        else if (rid == 13)
            p->srnPC = atoi(v);
        q = blockEnd;
    }
    p->have = p->uid > 0;
}

static void parse_legs(const char *xml, uint32_t len)
{
    const char *end = xml + len, *q = xml, *t, *tEnd;
    s_legCount = 0;
    while ((t = United_XmlBlock(q, end, "tournament", &tEnd)) != NULL) {
        char name[48], v[16], *hash;
        const char *m = t, *b, *bEnd;
        int32_t rid;
        United_XmlText(t, tEnd, "name", name, sizeof name);
        if ((hash = strstr(name, " (#")) != NULL)
            *hash = 0;
        United_XmlText(t, tEnd, "rankings_id", v, sizeof v);
        rid = atoi(v);
        while (s_legCount < MAX_LEGS && (b = United_XmlBlock(m, tEnd, "match", &bEnd)) != NULL) {
            OpenLeg *l = &s_legs[s_legCount++];
            uint32_t i;
            United_XmlText(b, bEnd, "match_id", v, sizeof v);
            l->matchId = (uint32_t)strtoul(v, NULL, 10);
            United_XmlText(b, bEnd, "match_leg_id", v, sizeof v);
            l->legId = (uint32_t)strtoul(v, NULL, 10);
            l->rankingId = rid;
            for (i = 0; name[i] && i < sizeof l->tournament - 1; i++)
                l->tournament[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
            l->tournament[i] = 0;
            United_XmlText(b, bEnd, "home_user", l->home, sizeof l->home);
            United_XmlText(b, bEnd, "away_user", l->away, sizeof l->away);
            m = bEnd;
        }
        q = tEnd;
    }
    s_legsLoaded = 1;
}

static int32_t s_rankingsPending;

/* api/rankings/users: this player's and the other's SRN in that ranking (5 Amiga, 13 PC). */
static void parse_ranking(const char *body, uint32_t len, int32_t rid)
{
    UnitedJson all = { body, (int32_t)len }, users, u;
    int32_t pos = 0;
    if (!United_JsonFind(all, "$.users", &users))
        return;
    while (United_JsonNext(users, &pos, NULL, &u)) {
        char name[40];
        Player *p = NULL;
        United_JsonText(u, "$.name", name, sizeof name);
        if (SDL_strcasecmp(name, United_UserName()) == 0)
            p = &s_me;
        else if (s_peer.name[0] && SDL_strcasecmp(name, s_peer.name) == 0)
            p = &s_peer;
        if (!p)
            continue;
        if (rid == 13)
            p->srnPC = United_JsonInt(u, "$.rating");
        else
            p->srnAmiga = United_JsonInt(u, "$.rating");
    }
}

/* api/open-matches: {"league_matches":[{"match_id","leg_id","duel_ranking_id","context_name",
 * "home_username","away_username",...}]} -- this player's open league and cup legs. */
static void parse_legs_json(const char *body, uint32_t len)
{
    UnitedJson all = { body, (int32_t)len }, list, m;
    int32_t pos = 0;
    s_legCount = 0;
    if (United_JsonFind(all, "$.league_matches", &list))
        while (s_legCount < MAX_LEGS && United_JsonNext(list, &pos, NULL, &m)) {
            OpenLeg *l = &s_legs[s_legCount++];
            char name[48], *hash;
            uint32_t i;
            l->matchId = (uint32_t)United_JsonInt(m, "$.match_id");
            l->legId = (uint32_t)United_JsonInt(m, "$.leg_id");
            l->rankingId = United_JsonInt(m, "$.duel_ranking_id");
            United_JsonText(m, "$.context_name", name, sizeof name);
            if ((hash = strstr(name, " (#")) != NULL)
                *hash = 0;
            for (i = 0; name[i] && i < sizeof l->tournament - 1; i++)
                l->tournament[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
            l->tournament[i] = 0;
            United_JsonText(m, "$.home_username", l->home, sizeof l->home);
            United_JsonText(m, "$.away_username", l->away, sizeof l->away);
        }
    s_legsLoaded = 1;
}

static void srn_lines(void);
static void submit(uint32_t legId, const AccountResult *r);

/* A JSON string's contents: quotes and backslashes escaped, control characters dropped. */
static void json_escape(const char *in, char *out, uint32_t cap)
{
    uint32_t n = 0;
    for (; *in && n + 2 < cap; in++) {
        if ((uint8_t)*in < 0x20)
            continue;
        if (*in == '"' || *in == '\\')
            out[n++] = '\\';
        out[n++] = *in;
    }
    out[n] = 0;
}

static void job_done(const Job *j, const HttpsReply *r)
{
    const int32_t ok = r->status == 200 && r->body;
    switch (j->kind) {
    case JOB_LIMITS:
        if (ok) {
            const char *p = strstr(r->body, "[SRN_DIFF_LIMIT]");
            if (p && sscanf(p + 16, "%d %d", (int *)&s_limit, (int *)&s_limit2) == 2)
                s_limitsLoaded = 1;
        }
        break;
    case JOB_MY_DETAIL:
        if (ok)
            parse_user(r->body, r->length, &s_me);
        s_me.failed = !s_me.have;
        srn_lines();
        break;
    case JOB_PEER_DETAIL:
        if (ok)
            parse_user(r->body, r->length, &s_peer);
        s_peer.failed = !s_peer.have;
        srn_lines();
        break;
    case JOB_MY_MATCHES:
        if (ok)
            parse_legs(r->body, r->length);
        else
            s_legsLoaded = -1;
        break;
    case JOB_CREATE: {
        char id[32];
        unsigned long mid = 0, lid = 0;
        if (ok && United_XmlText(r->body, r->body + r->length, "id", id, sizeof id) &&
            sscanf(id, "%lu-%lu", &mid, &lid) == 2 && lid) {
            s_rankedMatchId = (uint32_t)mid;
            s_rulesMatch = (uint32_t)mid;
            s_rulesLeg = (uint32_t)lid;
            s_haveRules = 1;            /* (into the rules when the lobby is open again: the joiner's) */
            fprintf(stderr, "[united] ranked match created: %lu-%lu\n", mid, lid);
            if (s_hostPending) {
                s_hostPending = 0;
                submit((uint32_t)lid, &s_hostResult);
            }
        } else {
            s_hostPending = 0;
            NetplayLobby_ChatNotice("THE SITE DID NOT CREATE THE RANKED MATCH");
            fprintf(stderr, "[united] ranked match not created (%d)\n", (int)r->status);
        }
        break;
    }
    case JOB_RANKING:
        if (ok)
            parse_ranking(r->body, r->length, (int32_t)j->legId);
        else
            s_me.failed = s_peer.failed = 1;
        if (--s_rankingsPending <= 0 && !s_me.failed) {
            s_me.have = United_UserId() > 0;
            s_peer.have = 1;            /* (not in the ranking: unranked, 1000) */
        }
        srn_lines();
        break;
    case JOB_N_CREATE:
    case JOB_N_ADDLEG: {
        UnitedJson all = { r->body, (int32_t)r->length }, legs, first;
        int32_t pos = 0;
        uint32_t mid = 0, lid = 0;
        if (ok) {
            mid = (uint32_t)United_JsonInt(all, "$.match_id");
            if (j->kind == JOB_N_ADDLEG)
                lid = (uint32_t)United_JsonInt(all, "$.leg_id");
            else if (United_JsonFind(all, "$.open_leg_ids", &legs) && United_JsonNext(legs, &pos, NULL, &first))
                lid = (uint32_t)atol(first.p);
        }
        if (mid && lid) {
            s_rankedMatchId = s_sessionId = mid;
            s_rulesMatch = mid;
            s_rulesLeg = lid;
            s_haveRules = 1;            /* (into the rules when the lobby is open again: the joiner's) */
            fprintf(stderr, "[united] ranked leg made on swos.swosunited.com: match %u leg %u\n", (unsigned)mid,
                    (unsigned)lid);
            if (s_hostPending) {
                s_hostPending = 0;
                submit(lid, &s_hostResult);
            }
        } else {
            char status[40];
            United_JsonText(all, "$.status", status, sizeof status);
            s_hostPending = 0;
            NetplayLobby_ChatNotice("THE SITE DID NOT CREATE THE RANKED MATCH");
            fprintf(stderr, "[united] ranked match not created (%d %s)\n", (int)r->status, status);
        }
        break;
    }
    case JOB_N_REPORT: {
        UnitedJson all = { r->body, (int32_t)r->length };
        char status[40];
        United_JsonText(all, "$.status", status, sizeof status);
        if (strcmp(status, "applied") == 0 || strcmp(status, "waiting") == 0 || strcmp(status, "not_found") == 0) {
            s_lastApplied = status[0] == 'a';
            NetplayLobby_ChatNotice(status[0] == 'w' ? "MATCH SCORE SENT, WAITING FOR THE OTHER PLAYER"
                                                     : "MATCH SCORE CONFIRMED");
            if (s_doneCount < (int32_t)(sizeof s_done / sizeof s_done[0]))
                s_done[s_doneCount++] = j->legId;
        } else {
            NetplayLobby_ChatNotice(strcmp(status, "conflict") == 0 ? "THE TWO SCORES DIFFER: NOT CONFIRMED"
                                                                    : "MATCH SCORE NOT CONFIRMED");
            fprintf(stderr, "[united] result for leg %u not taken (%d %s)\n", (unsigned)j->legId, (int)r->status,
                    status);
        }
        break;
    }
    case JOB_N_OPEN:
        if (ok && United_JsonOk(r->body, r->length))
            parse_legs_json(r->body, r->length);
        else
            s_legsLoaded = -1;
        break;
    case JOB_N_CLOSE: {
        UnitedJson all = { r->body, (int32_t)r->length };
        char status[40];
        United_JsonText(all, "$.status", status, sizeof status);
        fprintf(stderr, "[united] ranked session closed: %s\n", status);
        break;
    }
    case JOB_SUBMIT: {
        char msg[64];
        if (ok && United_XmlText(r->body, r->body + r->length, "message", msg, sizeof msg) &&
            SDL_strcasecmp(msg, "Saved") == 0) {
            NetplayLobby_ChatNotice("MATCH SCORE CONFIRMED");
            if (s_doneCount < (int32_t)(sizeof s_done / sizeof s_done[0]))
                s_done[s_doneCount++] = j->legId;
        } else {
            NetplayLobby_ChatNotice("MATCH SCORE NOT CONFIRMED");
            fprintf(stderr, "[united] result for leg %u not taken (%d): %.120s\n", (unsigned)j->legId,
                    (int)r->status, r->body ? r->body : "");
        }
        break;
    }
    }
}

static void jobs_pump(void)
{
    HttpsReply r;
    if (s_running) {
        if (!Https_PollAsync(&r))
            return;
        s_running = 0;
        fprintf(stderr, "[united] job %d: %d\n", (int)s_current.kind, (int)r.status);
        job_done(&s_current, &r);
        Https_Free(&r);
    }
    if (!s_jobCount)
        return;
    s_current = s_jobs[0];
    if (getenv("SWOS_UNITED_DRY_RUN") && (s_current.kind == JOB_N_CREATE || s_current.kind == JOB_N_ADDLEG ||
                                          s_current.kind == JOB_N_REPORT || s_current.kind == JOB_N_CLOSE)) {
        /* The same for the new site: the body without its api_key, and a made-up answer. */
        static uint32_t dryNewLeg = 800000;
        const char *body = strchr(s_current.form, ',');
        char reply[160];
        fprintf(stderr, "[united] dry run: %s {%s\n", strrchr(s_current.url, '/') + 1, body ? body + 1 : "}");
        if (s_current.kind == JOB_N_CREATE)
            snprintf(reply, sizeof reply, "{\"status\":\"ok\",\"match_id\":700001,\"leg_ids\":[%u],"
                     "\"open_leg_ids\":[%u],\"created\":true,\"legs_added\":1}", (unsigned)(dryNewLeg + 1),
                     (unsigned)(dryNewLeg + 1));
        else if (s_current.kind == JOB_N_ADDLEG)
            snprintf(reply, sizeof reply, "{\"status\":\"ok\",\"match_id\":700001,\"leg_id\":%u}",
                     (unsigned)(dryNewLeg + 1));
        else if (s_current.kind == JOB_N_REPORT)
            snprintf(reply, sizeof reply, "{\"status\":\"%s\"}", s_host ? "waiting" : "applied");
        else
            snprintf(reply, sizeof reply, "{\"status\":\"closed\",\"match_id\":700001}");
        if (s_current.kind == JOB_N_CREATE || s_current.kind == JOB_N_ADDLEG)
            dryNewLeg++;
        r.status = 200;
        r.body = reply;
        r.length = (uint32_t)strlen(reply);
        memmove(&s_jobs[0], &s_jobs[1], (size_t)(s_jobCount - 1) * sizeof s_jobs[0]);
        s_jobCount--;
        job_done(&s_current, &r);
        return;
    }
    if (getenv("SWOS_UNITED_DRY_RUN") && (s_current.kind == JOB_CREATE || s_current.kind == JOB_SUBMIT)) {
        /* A test that must not change the site's rankings: what would have gone, without the
         * password or the site's key, and the answer the site gives when it takes it. */
        static uint32_t dryLeg = 900000;
        char shown[384], *cut, reply[96];
        snprintf(shown, sizeof shown, "%s", s_current.kind == JOB_SUBMIT ? s_current.form : s_current.url);
        if ((cut = strstr(shown, s_current.kind == JOB_SUBMIT ? "&username=" : "&hash=")) != NULL)
            *cut = 0;
        fprintf(stderr, "[united] dry run: %s %s\n", s_current.kind == JOB_SUBMIT ? "matchleginsert" : "matchcreate",
                s_current.kind == JOB_SUBMIT ? shown : strstr(shown, "format="));
        if (s_current.kind == JOB_SUBMIT)
            snprintf(reply, sizeof reply, "<scorecard><message>Saved</message></scorecard>");
        else
            snprintf(reply, sizeof reply, "<match><id>%u-%u</id></match>",
                     (unsigned)(s_rankedMatchId ? s_rankedMatchId : 1), (unsigned)++dryLeg);
        r.status = 200;
        r.body = reply;
        r.length = (uint32_t)strlen(reply);
        memmove(&s_jobs[0], &s_jobs[1], (size_t)(s_jobCount - 1) * sizeof s_jobs[0]);
        s_jobCount--;
        job_done(&s_current, &r);
        return;
    }
    if (Https_RequestAsync(s_current.url, s_current.form[0] ? s_current.form : NULL) != 0)
        return;                         /* (the slot is busy: next frame) */
    memmove(&s_jobs[0], &s_jobs[1], (size_t)(s_jobCount - 1) * sizeof s_jobs[0]);
    s_jobCount--;
    s_running = 1;
}

static void ask_limits(void)
{
    if (!s_limitsLoaded)
        job_add(JOB_LIMITS, "https://sensiblesoccer.de/swos2020/login_check.php", NULL, 0);
}

static void ask_user(JobKind kind, const char *name)
{
    char up[32], esc[96], query[160];
    uint32_t i;
    for (i = 0; name[i] && i < sizeof up - 1; i++)
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
    up[i] = 0;
    Https_Escape(up, esc, sizeof esc);
    snprintf(query, sizeof query, "usr=%s&format=xml", esc);
    job_add(kind, United_ApiUrl("userdetail", query), NULL, 0);
}

static void ask_legs(void)
{
    char query[64];
    if (United_NewSite()) {
        char key[96], form[160];
        json_escape(United_ApiKey(), key, sizeof key);
        snprintf(form, sizeof form, "{\"api_key\":\"%s\"}", key);
        s_legsLoaded = 0;
        job_add(JOB_N_OPEN, United_NewApiUrl("open-matches", NULL), form, 0);
        return;
    }
    snprintf(query, sizeof query, "uid=%d&format=xml", (int)United_UserId());
    s_legsLoaded = 0;
    job_add(JOB_MY_MATCHES, United_ApiUrl("matches", query), NULL, 0);
}

/* The new site: both rankings in full, for this player's SRN and the other's. */
static void ask_rankings(void)
{
    static const int32_t ids[2] = { 5, 13 };
    int32_t i;
    s_rankingsPending = 0;
    for (i = 0; i < 2; i++) {
        char query[48];
        snprintf(query, sizeof query, "ranking_id=%d", (int)ids[i]);
        job_add(JOB_RANKING, United_NewApiUrl("rankings/users", query), NULL, (uint32_t)ids[i]);
        s_rankingsPending++;
    }
}

/* ---- the SRN, and whether a ranked match may be played ---- */

static int32_t ranked_allowed(int32_t a, int32_t b, int32_t amiga)
{
    int32_t ok = (a >= s_limit2 && b >= s_limit2) || abs(a - b) <= s_limit;
    if (amiga && (a >= 2100 || b >= 2100))      /* (SWOS 2020's Amiga rule at the top) */
        ok = (a >= 2100 && b >= 1500) || (b >= 2100 && a >= 1500);
    return ok;
}

static void srn_lines(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    char line[64];
    if (s_srnTold)
        return;
    if (s_me.failed || s_peer.failed) {         /* (the site did not say: no ranked match) */
        s_srnTold = 1;
        if (s_host && rules->mode == LOBBY_MODE_RANKED) {
            LobbyRules r = *rules;
            NetplayLobby_ChatNotice("COULD NOT CHECK THE SRN");
            NetplayLobby_ChatNotice("CONTINUE TO PLAY FRIENDLY");
            r.mode = LOBBY_MODE_FRIENDLY;
            NetplayLobby_SetRules(&r);
        }
        return;
    }
    if (!s_me.have || !s_peer.have)
        return;
    s_srnTold = 1;
    snprintf(line, sizeof line, "SRN OF %s : %d", United_UserName(), (int)srn_for_style(&s_me, _gDosGameplay));
    NetplayLobby_ChatNotice(line);
    snprintf(line, sizeof line, "SRN OF %s : %d", s_peer.name, (int)srn_for_style(&s_peer, _gDosGameplay));
    NetplayLobby_ChatNotice(line);
    if (s_host && rules->mode == LOBBY_MODE_RANKED &&
        !ranked_allowed(srn_for_style(&s_me, _gDosGameplay), srn_for_style(&s_peer, _gDosGameplay),
                        !_gDosGameplay)) {
        LobbyRules r = *rules;
        snprintf(line, sizeof line, "SRN DIFF CANNOT EXCEED %d", (int)s_limit);
        NetplayLobby_ChatNotice(line);
        NetplayLobby_ChatNotice("CONTINUE TO PLAY FRIENDLY");
        r.mode = LOBBY_MODE_FRIENDLY;
        NetplayLobby_SetRules(&r);
    }
}

/* ---- OPEN: openLobbyMenu ---- */

static int32_t s_choice, s_notSigned, s_waitLegs, s_noKeys;
static char    s_desc[4][56];

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void open_url(const char *url)
{
    if (url && SDL_OpenURL(url) != 0)
        fprintf(stderr, "[united] could not open %s: %s\n", url, SDL_GetError());
}

static void openLobbyMenu_layout(void)
{
    static uint16_t headerX, headerW;
    MenuEntry *header = entry_at(OpenLobbyMenu_header);
    int32_t i;
    if (!headerW) {
        headerX = header->x;
        headerW = header->dwWidth;
    }
    if (s_notSigned) {
        extern uint8_t _currentMenu[];
        Menu *menu = (Menu *)_currentMenu;
        header->fg = (uintptr_t)"LEAGUE / TOURNAMENT";
        header->dwWidth = 120;                  /* (the title is longer than OPEN LOBBY's box) */
        header->x = (uint16_t)(headerX + headerW / 2 - 60);
        entry_at(OpenLobbyMenu_friendly)->isInvisible = 1;
        if (menu->selectedEntry == entry_at(OpenLobbyMenu_friendly))
            menu->selectedEntry = entry_at(OpenLobbyMenu_ranked);
        entry_at(OpenLobbyMenu_ranked)->fg = (uintptr_t)"ONLINE LEAGUES";
        entry_at(OpenLobbyMenu_leagueAndTournament)->fg = (uintptr_t)"JOIN DISCORD TO APPLY";
        snprintf(s_desc[0], sizeof s_desc[0], "YOU ARE CURRENTLY NOT SIGNED FOR");
        snprintf(s_desc[1], sizeof s_desc[1], "TOURNAMENTS OR ONLINE LEAGUES.");
        snprintf(s_desc[2], sizeof s_desc[2], "DO YOU WANT TO JOIN AND COMPETE?");
        snprintf(s_desc[3], sizeof s_desc[3], "HERE ARE SOME LINKS FOR YOU.");
    } else {
        header->fg = (uintptr_t)"OPEN LOBBY";
        header->x = headerX;
        header->dwWidth = headerW;
        entry_at(OpenLobbyMenu_friendly)->isInvisible = 0;
        entry_at(OpenLobbyMenu_ranked)->fg = (uintptr_t)"RANKED MATCH";
        entry_at(OpenLobbyMenu_leagueAndTournament)->fg = (uintptr_t)"LEAGUE / CUP";
        if (s_waitLegs) {
            snprintf(s_desc[0], sizeof s_desc[0], "%s", "");
            snprintf(s_desc[1], sizeof s_desc[1], "LOOKING FOR YOUR LEAGUE AND CUP MATCHES...");
            s_desc[2][0] = s_desc[3][0] = 0;
        } else if (s_noKeys) {                  /* (RANKED MATCH pressed in a build without the keys) */
            snprintf(s_desc[0], sizeof s_desc[0], "THIS BUILD CANNOT HOST RANKED MATCHES:");
            snprintf(s_desc[1], sizeof s_desc[1], "IT WAS MADE WITHOUT SENSIBLESOCCER.DE'S KEYS.");
            snprintf(s_desc[2], sizeof s_desc[2], "YOU CAN STILL JOIN A RANKED MATCH");
            snprintf(s_desc[3], sizeof s_desc[3], "THAT ANOTHER PLAYER HOSTS.");
        } else {
            snprintf(s_desc[0], sizeof s_desc[0], "IN CASE OF RANKED MATCHES,");
            snprintf(s_desc[1], sizeof s_desc[1], "SRN DIFFERENCE CANNOT EXCEED %d.", (int)s_limit);
            snprintf(s_desc[2], sizeof s_desc[2], "OTHERWISE MATCH WILL BE AUTO-FRIENDLY.");
            snprintf(s_desc[3], sizeof s_desc[3], "EXCEPTION: BOTH PLAYERS HAVE MORE THAN %d SRN.", (int)s_limit2);
        }
    }
    for (i = 0; i < 4; i++)
        entry_at(OpenLobbyMenu_description1 + i)->fg = (uintptr_t)s_desc[i];
}

static void openLobbyMenu_OnInit(void)
{
    ask_limits();
    openLobbyMenu_layout();
}

static void openLobbyMenu_OnDraw(void)
{
    jobs_pump();
    if (s_waitLegs && s_legsLoaded) {
        s_waitLegs = 0;
        if (s_legsLoaded > 0 && s_legCount > 0) {
            s_choice = LOBBY_MODE_LEAGUE;
            SetExitMenuFlag();
        } else {
            s_notSigned = 1;
        }
    }
    openLobbyMenu_layout();
}

static void selectFriendly(void)
{
    s_choice = LOBBY_MODE_FRIENDLY;
    SetExitMenuFlag();
}

static void selectRanked(void)
{
    if (s_notSigned) {
        open_url(Account_Url(ACCOUNT_LINK_LEAGUES));
        return;
    }
    if (!United_NewSite() && !UNITED_RANKED_CREATE_KEY[0]) {     /* (a build without the old site's keys) */
        s_noKeys = 1;                           /* (said by openLobbyMenu_layout, which redraws the lines) */
        return;
    }
    s_choice = LOBBY_MODE_RANKED;
    SetExitMenuFlag();
}

static void selectLeagueAndTournament(void)
{
    if (s_notSigned) {
        open_url(Account_Url(ACCOUNT_LINK_DISCORD));
        return;
    }
    if (s_waitLegs)
        return;
    s_waitLegs = 1;
    ask_legs();
}

static void selectExitOpenLobby(void)
{
    s_choice = -1;
    SetExitMenuFlag();
}

int32_t Account_ChooseMode(void)
{
    s_choice = -1;
    s_notSigned = s_waitLegs = s_noKeys = 0;
    OnlineMenus_Show("swos2020/openLobbyMenu");
    return s_choice;
}

/* ---- the lobby ---- */

void Account_LobbyOpened(int32_t host)
{
    s_host = host;
    s_srnTold = 0;
    s_testSelected = 0;
    s_haveRules = 0;
    s_guestWaiting = 0;
    s_hostPending = 0;
    s_rankedMatchId = 0;
    s_sessionId = 0;
    s_lastApplied = s_rankingsAsked = 0;
    memset(&s_peer, 0, sizeof s_peer);
    memset(&s_me, 0, sizeof s_me);
    s_me.srnAmiga = s_me.srnPC = s_peer.srnAmiga = s_peer.srnPC = UNRANKED;
    ask_limits();
    if (!United_NewSite())
        ask_user(JOB_MY_DETAIL, United_UserName());
    ask_legs();
}

void Account_LobbyPeer(const char *name)
{
    snprintf(s_peer.name, sizeof s_peer.name, "%s", name);
    if (!United_NewSite()) {
        ask_user(JOB_PEER_DETAIL, name);
    } else if (!s_rankingsAsked) {
        s_rankingsAsked = 1;
        ask_rankings();
    }
}

/* Leaving the lobby: the new site's ranked session closed (its Elo is counted only then) by the
 * player whose report applied the last leg, and whatever is still queued sent first -- up to a
 * few seconds, the lobby's link already finished with. */
void Account_LobbyClosed(void)
{
    const uint32_t session = s_sessionId ? s_sessionId : NetplayLobby_Rules()->matchId;
    uint32_t waited = 0;
    if (United_NewSite() && session && s_lastApplied && NetplayLobby_Rules()->mode == LOBBY_MODE_RANKED) {
        char key[96], form[200];
        json_escape(United_ApiKey(), key, sizeof key);
        snprintf(form, sizeof form, "{\"api_key\":\"%s\",\"match_id\":%u}", key, (unsigned)session);
        job_add(JOB_N_CLOSE, United_NewApiUrl("swos-matches/close", NULL), form, 0);
    }
    while ((s_jobCount || s_running) && waited < 8000) {
        jobs_pump();
        SDL_Delay(10);
        waited += 10;
    }
    s_sessionId = 0;
    s_lastApplied = 0;
}

void Account_LobbyPoll(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    jobs_pump();
    /* SWOS_UNITED_TEST_SELECT: a test's host presses SELECT MATCH once it can */
    if (getenv("SWOS_UNITED_TEST_SELECT") && s_host && rules->mode == LOBBY_MODE_LEAGUE && !rules->legId &&
        s_legsLoaded > 0 && s_peer.name[0] && !s_testSelected) {
        s_testSelected = 1;
        Account_SelectMatch();
    }
    /* host: a ranked match the site has just made, into the rules -- the joiner's ids */
    if (s_host && s_haveRules && NetplayLobby_State() == LOBBY_OPEN) {
        LobbyRules r = *rules;
        s_haveRules = 0;
        r.matchId = s_rulesMatch;
        r.legId = s_rulesLeg;
        NetplayLobby_SetRules(&r);
    }
    /* joiner: the leg id of the ranked match just played arrives with the host's rules */
    if (!s_host && s_guestWaiting && rules->mode == LOBBY_MODE_RANKED && rules->legId &&
        rules->legId != s_guestLegBefore) {
        s_guestWaiting = 0;
        if (rules->matchId)
            s_sessionId = rules->matchId;
        submit(rules->legId, &s_guestResult);
    }
}

/* START waits for the SRN check of a ranked match, and for SELECT MATCH of a league one. */
int32_t Account_LobbyCanStart(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    if (rules->mode == LOBBY_MODE_RANKED)
        return s_srnTold;
    if (rules->mode == LOBBY_MODE_LEAGUE)
        return rules->legId != 0;
    return 1;
}

static const OpenLeg *leg_by_id(uint32_t legId)
{
    int32_t i;
    for (i = 0; i < s_legCount; i++)
        if (s_legs[i].legId == legId)
            return &s_legs[i];
    return NULL;
}

const char *Account_MatchText(void)
{
    static char text[40];
    const LobbyRules *rules = NetplayLobby_Rules();
    if (rules->mode == LOBBY_MODE_LEAGUE) {
        const OpenLeg *l = rules->legId ? leg_by_id(rules->legId) : NULL;
        if (!rules->legId)
            return "(MATCH NOT SELECTED YET)";
        snprintf(text, sizeof text, "MATCH FOR %.17s", l ? l->tournament : "A TOURNAMENT");
        return text;
    }
    if (rules->mode == LOBBY_MODE_RANKED)
        return "RANKED MATCH";
    return "(NO MATCH INFO)";
}

/* This leg's result has been taken -- or is on its way: queued or being sent (a rematch can end
 * before the last report has gone). */
static int32_t done(uint32_t legId)
{
    int32_t i;
    for (i = 0; i < s_doneCount; i++)
        if (s_done[i] == legId)
            return 1;
    for (i = 0; i < s_jobCount; i++)
        if ((s_jobs[i].kind == JOB_SUBMIT || s_jobs[i].kind == JOB_N_REPORT) && s_jobs[i].legId == legId)
            return 1;
    return s_running && (s_current.kind == JOB_SUBMIT || s_current.kind == JOB_N_REPORT) && s_current.legId == legId;
}

/* SELECT MATCH: the first open leg between the two of us in a tournament of this game's style. */
void Account_SelectMatch(void)
{
    const int32_t rid = _gDosGameplay ? 13 : 5;
    int32_t i, wrongStyle = 0;
    if (!s_host || s_legsLoaded <= 0)
        return;
    for (i = 0; i < s_legCount; i++) {
        const OpenLeg *l = &s_legs[i];
        const int32_t ours = (SDL_strcasecmp(l->home, United_UserName()) == 0 && SDL_strcasecmp(l->away, s_peer.name) == 0) ||
                             (SDL_strcasecmp(l->away, United_UserName()) == 0 && SDL_strcasecmp(l->home, s_peer.name) == 0);
        if (!ours || done(l->legId))
            continue;
        if (l->rankingId != rid) {
            wrongStyle = 1;
            continue;
        }
        {
            LobbyRules r = *NetplayLobby_Rules();
            r.matchId = l->matchId;
            r.legId = l->legId;
            NetplayLobby_SetRules(&r);
            NetplayLobby_ChatNotice("MATCH SELECTED");
            fprintf(stderr, "[united] selected %s, match %u leg %u\n", l->tournament, (unsigned)l->matchId,
                    (unsigned)l->legId);
        }
        return;
    }
    NetplayLobby_ChatNotice(wrongStyle ? "YOUR MATCH IS FOR THE OTHER GAME STYLE" :
                                         "NO OPEN LEAGUE OR CUP MATCH BETWEEN YOU");
}

/* ---- results ---- */

/* The new site's report: this player's goals and the other's; extra time and penalties only when
 * there were any, by the same rule on both machines (an omitted field and a 0 differ there). */
static void submit_new(uint32_t legId, const AccountResult *r)
{
    char key[96], form[384];
    int32_t n;
    json_escape(United_ApiKey(), key, sizeof key);
    n = snprintf(form, sizeof form, "{\"api_key\":\"%s\",\"leg_id\":%u,\"score_own\":%d,\"score_opponent\":%d",
                 key, (unsigned)legId, (int)r->mine[0], (int)r->theirs[0]);
    if (r->mine[1] || r->theirs[1] || r->mine[2] || r->theirs[2])
        n += snprintf(form + n, sizeof form - (uint32_t)n, ",\"extra_time_own\":%d,\"extra_time_opponent\":%d",
                      (int)r->mine[1], (int)r->theirs[1]);
    if (r->mine[2] || r->theirs[2])
        n += snprintf(form + n, sizeof form - (uint32_t)n, ",\"penalties_own\":%d,\"penalties_opponent\":%d",
                      (int)r->mine[2], (int)r->theirs[2]);
    snprintf(form + n, sizeof form - (uint32_t)n, "}");
    job_add(JOB_N_REPORT, United_NewApiUrl("swos-matches/report", NULL), form, legId);
}

static void submit(uint32_t legId, const AccountResult *r)
{
    char user[96], pass[160], form[384];
    char up[40];
    uint32_t i;
    const char *name = United_UserName();
    if (United_NewSite() && (r->mode == LOBBY_MODE_RANKED || r->mode == LOBBY_MODE_LEAGUE)) {
        submit_new(legId, r);
        return;
    }
    for (i = 0; name[i] && i < sizeof up - 1; i++)
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 'a' + 'A' : name[i]);
    up[i] = 0;
    Https_Escape(up, user, sizeof user);
    Https_Escape(United_Password(), pass, sizeof pass);
    /* extra time and penalties only when there were any (SWOS 2020 doAutoResultEntering) */
    if (!r->mine[1] && !r->theirs[1] && !r->mine[2] && !r->theirs[2])
        snprintf(form, sizeof form, "lid=%u&pg=%d&og=%d&format=xml&username=%s&password=%s", (unsigned)legId,
                 (int)r->mine[0], (int)r->theirs[0], user, pass);
    else if (!r->mine[2] && !r->theirs[2])
        snprintf(form, sizeof form, "lid=%u&pg=%d&og=%d&pe=%d&oe=%d&format=xml&username=%s&password=%s",
                 (unsigned)legId, (int)r->mine[0], (int)r->theirs[0], (int)r->mine[1], (int)r->theirs[1], user, pass);
    else
        snprintf(form, sizeof form, "lid=%u&pg=%d&og=%d&pe=%d&oe=%d&pp=%d&op=%d&format=xml&username=%s&password=%s",
                 (unsigned)legId, (int)r->mine[0], (int)r->theirs[0], (int)r->mine[1], (int)r->theirs[1],
                 (int)r->mine[2], (int)r->theirs[2], user, pass);
    job_add(JOB_SUBMIT, United_ApiUrl("matchleginsert", NULL), form, legId);
}

void Account_MatchPlayed(const AccountResult *r)
{
    if (!r->completed)
        return;
    if (r->mode == LOBBY_MODE_LEAGUE && r->legId && !done(r->legId)) {
        submit(r->legId, r);
        return;
    }
    if (r->mode != LOBBY_MODE_RANKED)
        return;
    if (r->host && United_NewSite()) {
        char key[96], opp[64], form[384];
        if (!s_peer.name[0])
            return;
        json_escape(United_ApiKey(), key, sizeof key);
        json_escape(s_peer.name, opp, sizeof opp);
        if (!s_rankedMatchId) {
            snprintf(form, sizeof form, "{\"api_key\":\"%s\",\"ranking_id\":%d,\"opponent_username\":\"%s\",\"leg_count\":1}",
                     key, r->dosStyle ? 13 : 5, opp);
            job_add(JOB_N_CREATE, United_NewApiUrl("swos-matches/create", NULL), form, 0);
        } else {
            snprintf(form, sizeof form, "{\"api_key\":\"%s\",\"match_id\":%u}", key, (unsigned)s_rankedMatchId);
            job_add(JOB_N_ADDLEG, United_NewApiUrl("swos-matches/add-leg", NULL), form, 0);
        }
        s_hostResult = *r;              /* (sent once the site has made the leg) */
        s_hostPending = 1;
        return;
    }
    if (r->host) {
        char query[256];
        if (!s_peer.uid || !s_me.uid)
            return;
        if (!s_rankedMatchId)
            snprintf(query, sizeof query, "format=xml&rid=%d&hid=%d&aid=%d&action=createOOO&hash=%s",
                     r->dosStyle ? 13 : 5, (int)s_me.uid, (int)s_peer.uid, UNITED_RANKED_CREATE_KEY);
        else
            snprintf(query, sizeof query, "format=xml&mid=%u&hid=%d&aid=%d&action=createOOOleg&hash=%s",
                     (unsigned)s_rankedMatchId, (int)s_me.uid, (int)s_peer.uid, UNITED_RANKED_ADDLEG_KEY);
        job_add(JOB_CREATE, United_ApiUrl("matchcreate", query), NULL, 0);
        s_hostResult = *r;              /* (sent once the site has made the leg: JOB_CREATE) */
        s_hostPending = 1;
        return;
    }
    s_guestResult = *r;
    s_guestWaiting = 1;
    s_guestLegBefore = NetplayLobby_Rules()->legId;
}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(openLobbyMenu_OnInit),
    HANDLER(openLobbyMenu_OnDraw),
    HANDLER(selectFriendly),
    HANDLER(selectRanked),
    HANDLER(selectLeagueAndTournament),
    HANDLER(selectExitOpenLobby),
};
#undef HANDLER

void UnitedMatches_Register(void)
{
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

#else

int32_t     Account_ChooseMode(void)            { return 0; }
void        Account_LobbyOpened(int32_t host)   { (void)host; }
void        Account_LobbyPeer(const char *name) { (void)name; }
void        Account_LobbyPoll(void)             {}
void        Account_LobbyClosed(void)           {}
int32_t     Account_LobbyCanStart(void)         { return 1; }
const char *Account_MatchText(void)             { return 0; }
void        Account_SelectMatch(void)           {}
void        Account_MatchPlayed(const AccountResult *result) { (void)result; }
void        UnitedMatches_Register(void)        {}

#endif
