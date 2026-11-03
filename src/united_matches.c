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
#include "platform_sdl.h"         /* Platform_OpenUrl */
#include "swos2020_menus.h"
#include "text_render.h"        /* GetTextWidth: a tournament name too wide for its cell */

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

typedef enum { JOB_LIMITS, JOB_MY_DETAIL, JOB_PEER_DETAIL, JOB_MY_MATCHES, JOB_CREATE, JOB_SUBMIT, JOB_TOURNA,
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
    char    country[32];                /* the old site's nation (a name), the new one's flag code */
} Player;

typedef struct {
    uint32_t matchId, legId, tournaId;
    int32_t  rankingId;
    int32_t  group;                     /* 1 a group (league) match, 0 a knockout one, -1 not known yet */
    char     tournament[32], home[24], away[24];
} OpenLeg;

static Player   s_me, s_peer;
static OpenLeg  s_picked;               /* (LEAGUE / CUP's picked leg: with tournaInfoMenu below) */
static int32_t  s_havePicked;
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
static int32_t  s_rulesNext;            /* (s_haveRules after a leg: the chat says NEXT MATCH SELECTED) */
static int32_t  done(uint32_t legId);
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
    {
        char *nl;
        United_XmlText(xml, end, "nation", p->country, sizeof p->country);
        while ((nl = strpbrk(p->country, "\r\n")) != NULL)       /* ("United States\n") */
            *nl = 0;
    }
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
        uint32_t tid;
        United_XmlText(t, tEnd, "name", name, sizeof name);
        if ((hash = strstr(name, " (#")) != NULL)
            *hash = 0;
        United_XmlText(t, tEnd, "rankings_id", v, sizeof v);
        rid = atoi(v);
        United_XmlText(t, tEnd, "id", v, sizeof v);
        tid = (uint32_t)strtoul(v, NULL, 10);
        while (s_legCount < MAX_LEGS && (b = United_XmlBlock(m, tEnd, "match", &bEnd)) != NULL) {
            OpenLeg *l = &s_legs[s_legCount++];
            uint32_t i;
            United_XmlText(b, bEnd, "match_id", v, sizeof v);
            l->matchId = (uint32_t)strtoul(v, NULL, 10);
            United_XmlText(b, bEnd, "match_leg_id", v, sizeof v);
            l->legId = (uint32_t)strtoul(v, NULL, 10);
            l->rankingId = rid;
            l->tournaId = tid;
            l->group = -1;                      /* (the tournament says: JOB_TOURNA) */
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
        if (!p->country[0])
            United_JsonText(u, "$.flag_code", p->country, sizeof p->country);
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
            l->tournaId = (uint32_t)United_JsonInt(m, "$.tournaments_id");
            l->group = United_JsonInt(m, "$.groups_id") > 0;     /* (0: a knockout round's) */
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
    case JOB_TOURNA: {
        /* view=tournament: each <round>'s <groups> count (0 a knockout round) and its matches'
         * <mid>: a leg's match in a group round is a group match, in a knockout one not; a
         * tournament with no knockout round has only group matches (SWOS 2020's
         * getNumOfGroupsAndLegs) */
        const char *q = ok ? r->body : NULL, *end = ok ? r->body + r->length : NULL, *b, *bEnd;
        int32_t i, anyKo = 0, found[MAX_LEGS];
        memset(found, -1, sizeof found);
        while (q && (b = United_XmlBlock(q, end, "round", &bEnd)) != NULL) {
            char v[16];
            int32_t ko;
            United_XmlText(b, bEnd, "groups", v, sizeof v);
            ko = atoi(v) == 0;
            anyKo |= ko;
            for (i = 0; i < s_legCount; i++) {
                char mid[24];
                snprintf(mid, sizeof mid, "<mid>%u</mid>", (unsigned)s_legs[i].matchId);
                if (s_legs[i].tournaId == j->legId && found[i] < 0) {
                    const char *hit = strstr(b, mid);
                    if (hit && hit < bEnd)
                        found[i] = !ko;
                }
            }
            q = bEnd;
        }
        for (i = 0; i < s_legCount; i++)
            if (s_legs[i].tournaId == j->legId && s_legs[i].group < 0)
                s_legs[i].group = found[i] >= 0 ? found[i] : ok && !anyKo ? 1 : -1;
        if (s_picked.tournaId == j->legId)
            for (i = 0; i < s_legCount; i++)
                if (s_legs[i].legId == s_picked.legId)
                    s_picked.group = s_legs[i].group;
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
/* LEAGUE / CUP's match, picked on SWOS 2020's tournaInfoMenu before the game is opened */
static OpenLeg  s_picked;
static int32_t  s_havePicked, s_pickTold;
static void     pick_match(void);
static char    s_desc[4][56];

static MenuEntry *entry_at(int32_t ordinal) { return CalcMenuEntryAddress((uint16_t)ordinal); }

static void open_url(const char *url)
{
    if (url)
        Platform_OpenUrl(url);          /* (says so in the log when it cannot) */
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
            pick_match();                       /* (SWOS 2020's selectMatchFromTourney) */
            if (s_havePicked) {
                s_choice = LOBBY_MODE_LEAGUE;
                SetExitMenuFlag();
            }
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

int32_t Account_ChosenStyle(void)
{
    return s_choice == LOBBY_MODE_LEAGUE && s_havePicked ? (s_picked.rankingId == 13 ? 1 : 0) : -1;
}

/* The selected league / cup leg, if it is known here. */
static const OpenLeg *current_leg(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    int32_t i;
    if (rules->mode != LOBBY_MODE_LEAGUE || !rules->legId)
        return NULL;
    for (i = 0; i < s_legCount; i++)
        if (s_legs[i].legId == rules->legId)
            return &s_legs[i];
    return s_havePicked && s_picked.legId == rules->legId ? &s_picked : NULL;
}

int32_t Account_CupLocked(void)
{
    const OpenLeg *l = current_leg();
    return l && l->group == 1;
}

int32_t Account_PlayerInfo(const char *name, char *country, uint32_t cap, int32_t *srn)
{
    const Player *p = !name ? NULL : SDL_strcasecmp(name, United_UserName()) == 0 ? &s_me :
                      SDL_strcasecmp(name, s_peer.name) == 0 ? &s_peer : NULL;
    if (!p || !p->have)
        return 0;
    snprintf(country, cap, "%s", p->country);
    *srn = _gDosGameplay ? p->srnPC : p->srnAmiga;
    return 1;
}

int32_t Account_JoinAllowed(const char *name)
{
    const char *opp;
    if (s_choice != LOBBY_MODE_LEAGUE || !s_havePicked || !name)
        return 1;
    opp = SDL_strcasecmp(s_picked.home, United_UserName()) == 0 ? s_picked.away : s_picked.home;
    return SDL_strcasecmp(name, opp) == 0;
}

int32_t Account_ChooseMode(void)
{
    s_choice = -1;
    s_notSigned = s_waitLegs = s_noKeys = 0;
    s_havePicked = 0;
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
    s_pickTold = 0;
    if (host && s_havePicked && NetplayLobby_Rules()->mode == LOBBY_MODE_LEAGUE) {
        s_rulesMatch = s_picked.matchId;        /* (into the rules once the lobby is open: LobbyPoll) */
        s_rulesLeg = s_picked.legId;
        s_haveRules = 1;
        fprintf(stderr, "[united] hosting %s: match %u leg %u, %s v %s\n", s_picked.tournament,
                (unsigned)s_picked.matchId, (unsigned)s_picked.legId, s_picked.home, s_picked.away);
    }
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
    /* a league / cup leg: whether it is a group match, from its tournament (the old site's
     * legs do not say), once a tournament; then a group match plays no cup */
    {
        static uint32_t asked[8];
        const OpenLeg *l = current_leg();
        int32_t i;
        if (l && l->group < 0 && l->tournaId && !United_NewSite()) {
            for (i = 0; i < 8 && asked[i] && asked[i] != l->tournaId; i++)
                ;
            if (i < 8 && !asked[i]) {
                char query[48];
                asked[i] = l->tournaId;
                snprintf(query, sizeof query, "id=%u&format=xml", (unsigned)l->tournaId);
                job_add(JOB_TOURNA, United_ApiUrl("tournament", query), NULL, l->tournaId);
            }
        }
        if (s_host && NetplayLobby_State() == LOBBY_OPEN && Account_CupLocked() && rules->legs != 0) {
            LobbyRules r = *rules;
            r.legs = 0;
            NetplayLobby_SetRules(&r);
            fprintf(stderr, "[united] a group match: the cup is off\n");
        }
    }
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
        if (rules->mode == LOBBY_MODE_LEAGUE)
            NetplayLobby_ChatNotice(!s_rulesLeg ? "NO OPEN MATCH LEFT BETWEEN YOU" :
                                    s_rulesNext ? "NEXT MATCH SELECTED" : "MATCH SELECTED");
        s_rulesNext = 0;
        if (!s_rulesLeg)
            s_testSelected = 0;                 /* (SWOS_UNITED_TEST_SELECT: the host picks again) */
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
    if (rules->mode == LOBBY_MODE_LEAGUE) {
        /* the match picked before opening is between this host and one player: anyone else
         * waits (SELECT MATCH can still pick one of ours against them) */
        if (s_host && s_havePicked && rules->legId == s_picked.legId && s_peer.name[0]) {
            const char *opp = SDL_strcasecmp(s_picked.home, United_UserName()) == 0 ? s_picked.away : s_picked.home;
            if (SDL_strcasecmp(opp, s_peer.name) != 0) {
                if (!s_pickTold) {
                    char line[64];
                    s_pickTold = 1;
                    snprintf(line, sizeof line, "THIS MATCH IS AGAINST %s", opp);
                    NetplayLobby_ChatNotice(line);
                }
                return 0;
            }
        }
        return rules->legId != 0 && !done(rules->legId);      /* (a played leg is not played again) */
    }
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
    return "(NO MATCH SELECTED)";                     /* (SWOS 2020's words) */
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

/* The host, after a league or cup leg: the same match's next open leg (a two-legged tie's second,
 * SWOS 2020's getNextSelectedMatchInfoTemp) into the rules, so the rematch is that leg and not the
 * one just played -- which both players then skipped as done, so a second leg's score never went
 * to the site. None: the rules' leg is cleared and START waits for SELECT MATCH. */
static void next_leg(uint32_t matchId, uint32_t playedLeg)
{
    const int32_t rid = _gDosGameplay ? 13 : 5;
    const OpenLeg *pick = NULL;
    int32_t i;
    for (i = 0; i < s_legCount && !pick; i++) {
            const OpenLeg *l = &s_legs[i];
            const int32_t ours = (SDL_strcasecmp(l->home, United_UserName()) == 0 && SDL_strcasecmp(l->away, s_peer.name) == 0) ||
                                 (SDL_strcasecmp(l->away, United_UserName()) == 0 && SDL_strcasecmp(l->home, s_peer.name) == 0);
            if (!ours || l->matchId != matchId || l->legId == playedLeg || done(l->legId) || l->rankingId != rid)
                continue;
            pick = l;
    }
    s_rulesMatch = pick ? pick->matchId : 0;
    s_rulesLeg = pick ? pick->legId : 0;
    s_haveRules = 1;                    /* (into the rules when the lobby is open again) */
    s_rulesNext = 1;
    if (pick) {
        s_picked = *pick;
        fprintf(stderr, "[united] next leg: %s, match %u leg %u, %s v %s\n", pick->tournament,
                (unsigned)pick->matchId, (unsigned)pick->legId, pick->home, pick->away);
    } else {
        fprintf(stderr, "[united] no open leg left between us after leg %u\n", (unsigned)playedLeg);
    }
}

void Account_MatchPlayed(const AccountResult *r)
{
    if (!r->completed)
        return;
    if (r->mode == LOBBY_MODE_LEAGUE && r->legId) {
        if (!done(r->legId))
            submit(r->legId, r);
        if (r->host)
            next_leg(r->matchId, r->legId);
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

/* ---- tournaInfoMenu: SELECT A MATCH FOR AUTO RESULT ENTERING ----
 *
 * SWOS 2020's (src/menus/tournaInfo.mnu, tournaInfoMenu_OnDraw and its handlers in mpgame.cpp),
 * shown by LEAGUE / CUP once this player's open legs have come (view=matches or api/open-matches):
 * their tournaments on the left (NAME, RANKING: AMIGA or PC DOS), the one under the cursor in red,
 * and its legs on the right (HOME PLAYER, AWAY PLAYER: this player grey, the other dark blue, the
 * leg under the cursor red, its ids below). FIRE on a leg picks it: the game is opened for that
 * leg in its ranking's style (Account_ChosenStyle). EXIT goes back to OPEN's three choices.
 * SWITCH TO FRIENDLY MATCH is hidden here, as SWOS 2020 hides it while a game is being opened. */
enum { TI_ROWS = 8, TI_MAX = 32, kTiOrange = 4, kTiDarkBlue = 3, kTiGray = 7, kTiRed = 10 };

typedef struct { char name[32]; int32_t rankingId, first[MAX_LEGS], count; } TiTourna;
static TiTourna s_ti[TI_MAX];
static int32_t  s_tiCount, s_tiCur, s_tiPage, s_tiMatchPage;
static char     s_tiName[TI_ROWS][24], s_tiRank[TI_ROWS][8], s_tiHome[TI_ROWS][24], s_tiAway[TI_ROWS][24];
static char     s_tiHead1[24], s_tiHead2[24], s_tiPage1[16], s_tiPage2[16], s_tiInfo[32];

static void ti_group(void)
{
    int32_t i, t;
    s_tiCount = 0;
    for (i = 0; i < s_legCount; i++) {
        const OpenLeg *l = &s_legs[i];
        if (done(l->legId))
            continue;
        for (t = 0; t < s_tiCount; t++)
            if (strcmp(s_ti[t].name, l->tournament) == 0 && s_ti[t].rankingId == l->rankingId)
                break;
        if (t == s_tiCount) {
            if (s_tiCount == TI_MAX)
                continue;
            memset(&s_ti[t], 0, sizeof s_ti[t]);
            snprintf(s_ti[t].name, sizeof s_ti[t].name, "%s", l->tournament);
            s_ti[t].rankingId = l->rankingId;
            s_tiCount++;
        }
        if (s_ti[t].count < MAX_LEGS)
            s_ti[t].first[s_ti[t].count++] = i;
    }
}

static int32_t ti_pages(int32_t n) { return n > 0 ? (n + TI_ROWS - 1) / TI_ROWS : 1; }

static void ti_upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 'a' + 'A');
}

/* SWOS 2020's shortenName: the glyph that goes past the limit becomes a '.'. */
static void ti_shorten(char *s, int32_t limit)
{
    extern uint8_t _smallCharsTable[];
    const int32_t spacing = ((CharsTableHeader *)_smallCharsTable)->charSpacing;
    int32_t i, w = 0;
    for (i = 0; s[i]; i++) {
        uint8_t one[2] = { (uint8_t)s[i], 0 };
        w += GetTextWidth(one, _smallCharsTable) - spacing;
        if (w > limit) {
            s[i] = '.';
            s[i + 1] = 0;
            break;
        }
    }
}

static MenuEntry *ti_selected(void)
{
    extern uint8_t _currentMenu[];
    return ((Menu *)_currentMenu)->selectedEntry;
}

static void ti_arrow_AfterDraw(MenuEntry *e)
{
    const int32_t down = e == entry_at(TournaInfoMenu_scrollDown) || e == entry_at(TournaInfoMenu_scrollDown2);
    United_DrawPageArrow(down, e->x + 2, e->y + 1);
}

static void ti_cell(int32_t ordinal, const char *text, uint16_t color)
{
    MenuEntry *e = entry_at(ordinal);
    e->fg = (uintptr_t)text;
    e->background = ENTRY_FRAME_AND_BACK_COLOR;
    e->bgColor = color;
}

static void ti_link(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

/* The leg on row r of the right-hand list, or NULL. */
static const OpenLeg *ti_leg(int32_t r)
{
    const TiTourna *t = s_tiCur >= 0 && s_tiCur < s_tiCount ? &s_ti[s_tiCur] : NULL;
    const int32_t idx = r + s_tiMatchPage * TI_ROWS;
    return t && idx < t->count ? &s_legs[t->first[idx]] : NULL;
}

static void tournaInfoMenu_OnDraw(void)
{
    MenuEntry *sel = ti_selected();
    int32_t r;

    jobs_pump();
    /* the cursor on a tournament makes it the one shown (SWOS 2020's g_CurTourna) */
    for (r = 0; r < TI_ROWS; r++)
        if ((sel == entry_at(TournaInfoMenu_name_01 + r) || sel == entry_at(TournaInfoMenu_ranking_01 + r)) &&
            r + s_tiPage * TI_ROWS < s_tiCount && s_tiCur != r + s_tiPage * TI_ROWS) {
            s_tiCur = r + s_tiPage * TI_ROWS;
            s_tiMatchPage = 0;
        }

    snprintf(s_tiHead1, sizeof s_tiHead1, "TOURNAMENTS (%d)", (int)s_tiCount);
    snprintf(s_tiHead2, sizeof s_tiHead2, "MATCHES (%d)", (int)(s_tiCur < s_tiCount ? s_ti[s_tiCur].count : 0));
    entry_at(TournaInfoMenu_subHeader1)->fg = (uintptr_t)s_tiHead1;
    entry_at(TournaInfoMenu_subHeader2)->fg = (uintptr_t)s_tiHead2;
    entry_at(TournaInfoMenu_header)->isInvisible = 1;
    entry_at(TournaInfoMenu_header2)->isInvisible = 0;
    entry_at(TournaInfoMenu_friendlyMatch)->isInvisible = 1;
    ti_cell(TournaInfoMenu_name_00, "NAME", kTiOrange);
    ti_cell(TournaInfoMenu_ranking_00, "RANKING", kTiOrange);
    ti_cell(TournaInfoMenu_home_00, "HOME PLAYER", kTiOrange);
    ti_cell(TournaInfoMenu_away_00, "AWAY PLAYER", kTiOrange);

    s_tiInfo[0] = 0;
    for (r = 0; r < TI_ROWS; r++) {
        const int32_t t = r + s_tiPage * TI_ROWS;
        const OpenLeg *l = ti_leg(r);
        const int32_t on = l && (sel == entry_at(TournaInfoMenu_home_01 + r) || sel == entry_at(TournaInfoMenu_away_01 + r));
        s_tiName[r][0] = s_tiRank[r][0] = s_tiHome[r][0] = s_tiAway[r][0] = 0;
        if (t < s_tiCount) {
            snprintf(s_tiName[r], sizeof s_tiName[r], "%s", s_ti[t].name);
            ti_upper(s_tiName[r]);
            ti_shorten(s_tiName[r], 70 + 25 - 8 - 8);
            snprintf(s_tiRank[r], sizeof s_tiRank[r], "%s", s_ti[t].rankingId == 13 ? "PC DOS" : "AMIGA");
        }
        ti_cell(TournaInfoMenu_name_01 + r, s_tiName[r], t == s_tiCur && t < s_tiCount ? kTiRed : kTiGray);
        ti_cell(TournaInfoMenu_ranking_01 + r, s_tiRank[r], t == s_tiCur && t < s_tiCount ? kTiRed : kTiGray);
        if (l) {
            snprintf(s_tiHome[r], sizeof s_tiHome[r], "%s", l->home);
            snprintf(s_tiAway[r], sizeof s_tiAway[r], "%s", l->away);
            ti_upper(s_tiHome[r]);
            ti_upper(s_tiAway[r]);
            if (on)
                snprintf(s_tiInfo, sizeof s_tiInfo, "ID: %u-%u", (unsigned)l->matchId, (unsigned)l->legId);
        }
        ti_cell(TournaInfoMenu_home_01 + r, s_tiHome[r],
                on ? kTiRed : l && SDL_strcasecmp(l->home, United_UserName()) != 0 ? kTiDarkBlue : kTiGray);
        ti_cell(TournaInfoMenu_away_01 + r, s_tiAway[r],
                on ? kTiRed : l && SDL_strcasecmp(l->away, United_UserName()) != 0 ? kTiDarkBlue : kTiGray);
    }
    if (s_tiCount)
        snprintf(s_tiPage1, sizeof s_tiPage1, "(%d / %d)", (int)s_tiPage + 1, (int)ti_pages(s_tiCount));
    else
        s_tiPage1[0] = 0;
    if (s_tiCur < s_tiCount)
        snprintf(s_tiPage2, sizeof s_tiPage2, "(%d / %d)", (int)s_tiMatchPage + 1, (int)ti_pages(s_ti[s_tiCur].count));
    else
        s_tiPage2[0] = 0;
    entry_at(TournaInfoMenu_page1)->fg = (uintptr_t)s_tiPage1;
    entry_at(TournaInfoMenu_page2)->fg = (uintptr_t)s_tiPage2;
    entry_at(TournaInfoMenu_matchInfo)->fg = (uintptr_t)s_tiInfo;

    entry_at(TournaInfoMenu_scrollUp)->afterDrawCb = entry_at(TournaInfoMenu_scrollDown)->afterDrawCb =
        entry_at(TournaInfoMenu_scrollUp2)->afterDrawCb = entry_at(TournaInfoMenu_scrollDown2)->afterDrawCb =
            ti_arrow_AfterDraw;

    /* SWOS 2020's links */
    for (r = 0; r < TI_ROWS; r++) {
        const int32_t last = r == TI_ROWS - 1, upper = r < 3;
        ti_link(TournaInfoMenu_name_01 + r, -1, TournaInfoMenu_ranking_01 + r,
                r ? TournaInfoMenu_name_01 + r - 1 : -1, last ? TournaInfoMenu_mexit : TournaInfoMenu_name_01 + r + 1);
        ti_link(TournaInfoMenu_ranking_01 + r, TournaInfoMenu_name_01 + r, upper ? TournaInfoMenu_scrollUp : TournaInfoMenu_scrollDown,
                r ? TournaInfoMenu_ranking_01 + r - 1 : -1, last ? TournaInfoMenu_mexit : TournaInfoMenu_ranking_01 + r + 1);
        ti_link(TournaInfoMenu_home_01 + r, upper ? TournaInfoMenu_scrollUp : TournaInfoMenu_scrollDown, TournaInfoMenu_away_01 + r,
                r ? TournaInfoMenu_home_01 + r - 1 : -1, last ? TournaInfoMenu_mexit : TournaInfoMenu_home_01 + r + 1);
        ti_link(TournaInfoMenu_away_01 + r, TournaInfoMenu_home_01 + r, upper ? TournaInfoMenu_scrollUp2 : TournaInfoMenu_scrollDown2,
                r ? TournaInfoMenu_away_01 + r - 1 : -1, last ? TournaInfoMenu_mexit : TournaInfoMenu_away_01 + r + 1);
    }
    ti_link(TournaInfoMenu_scrollUp, TournaInfoMenu_ranking_01, TournaInfoMenu_home_01, -1, TournaInfoMenu_scrollDown);
    ti_link(TournaInfoMenu_scrollDown, TournaInfoMenu_ranking_01 + TI_ROWS - 1, TournaInfoMenu_home_01 + TI_ROWS - 1,
            TournaInfoMenu_scrollUp, TournaInfoMenu_mexit);
    ti_link(TournaInfoMenu_scrollUp2, TournaInfoMenu_away_01, -1, -1, TournaInfoMenu_scrollDown2);
    ti_link(TournaInfoMenu_scrollDown2, TournaInfoMenu_away_01 + TI_ROWS - 1, -1, TournaInfoMenu_scrollUp2, TournaInfoMenu_mexit);
    ti_link(TournaInfoMenu_mexit, -1, -1, TournaInfoMenu_ranking_01 + TI_ROWS - 1, -1);
}

static void ti_turn(int32_t *page, int32_t n, int32_t dir)
{
    if (n > 0)
        *page = (*page + dir + ti_pages(n)) % ti_pages(n);
}

static void tournaInfo_scrollUp_onSelect(void)    { ti_turn(&s_tiPage, s_tiCount, -1); }
static void tournaInfo_scrollDown_onSelect(void)  { ti_turn(&s_tiPage, s_tiCount, +1); }
static void tournaInfo_scrollUp2_onSelect(void)
{
    if (s_tiCur < s_tiCount)
        ti_turn(&s_tiMatchPage, s_ti[s_tiCur].count, -1);
}
static void tournaInfo_scrollDown2_onSelect(void)
{
    if (s_tiCur < s_tiCount)
        ti_turn(&s_tiMatchPage, s_ti[s_tiCur].count, +1);
}
static void tournaInfo_name_onSelect(void)    {}
static void tournaInfo_ranking_onSelect(void) {}
static void tournaInfo_friendlyMatch_OnSelect(void) {}

/* FIRE on a leg of this player's: it is the match (SWOS 2020's setSelectedMatchInfo). */
static void ti_pick_row(void)
{
    MenuEntry *sel = ti_selected();
    int32_t r;
    for (r = 0; r < TI_ROWS; r++)
        if (sel == entry_at(TournaInfoMenu_home_01 + r) || sel == entry_at(TournaInfoMenu_away_01 + r)) {
            const OpenLeg *l = ti_leg(r);
            if (!l || (SDL_strcasecmp(l->home, United_UserName()) != 0 && SDL_strcasecmp(l->away, United_UserName()) != 0))
                return;
            s_picked = *l;
            s_havePicked = 1;
            SetExitMenuFlag();
            return;
        }
}
static void tournaInfoMenu_HomeUser_OnSelect(void) { ti_pick_row(); }
static void tournaInfoMenu_AwayUser_OnSelect(void) { ti_pick_row(); }

static void pick_match(void)
{
    ti_group();
    s_tiCur = s_tiPage = s_tiMatchPage = 0;
    s_havePicked = 0;
    OnlineMenus_Show("swos2020/tournaInfoMenu");
}

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(openLobbyMenu_OnInit),
    HANDLER(openLobbyMenu_OnDraw),
    HANDLER(selectFriendly),
    HANDLER(selectRanked),
    HANDLER(selectLeagueAndTournament),
    HANDLER(selectExitOpenLobby),
    HANDLER(tournaInfoMenu_OnDraw),
    HANDLER(tournaInfo_scrollUp_onSelect),
    HANDLER(tournaInfo_scrollDown_onSelect),
    HANDLER(tournaInfo_scrollUp2_onSelect),
    HANDLER(tournaInfo_scrollDown2_onSelect),
    HANDLER(tournaInfo_name_onSelect),
    HANDLER(tournaInfo_ranking_onSelect),
    HANDLER(tournaInfo_friendlyMatch_OnSelect),
    HANDLER(tournaInfoMenu_HomeUser_OnSelect),
    HANDLER(tournaInfoMenu_AwayUser_OnSelect),
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
int32_t     Account_ChosenStyle(void)           { return -1; }
int32_t     Account_JoinAllowed(const char *name) { (void)name; return 1; }
int32_t     Account_PlayerInfo(const char *name, char *country, uint32_t cap, int32_t *srn)
{ (void)name; (void)country; (void)cap; (void)srn; return 0; }
int32_t     Account_CupLocked(void)             { return 0; }
int32_t     Account_LobbyCanStart(void)         { return 1; }
const char *Account_MatchText(void)             { return 0; }
void        Account_SelectMatch(void)           {}
void        Account_MatchPlayed(const AccountResult *result) { (void)result; }
void        UnitedMatches_Register(void)        {}

#endif
