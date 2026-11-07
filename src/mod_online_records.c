/*
 * mod_online_records.c -- SWOS United's online records (fork mod "online-records"): the
 * challenges' and speedruns' records online, as SWOS 2020 kept them. The challenges and speedrun
 * mods are OpenSWOS's, shared; this mod offers the service they ask, "online-records" (OpenSWOS
 * docs/MODS.md §33). Without it they play offline, as in OpenSWOS.
 *
 *   challenges.completed  the player's beaten challenges: "<category>\t<name>" lines
 *   challenges.stats      {"name","category"}: "BEATEN BY 12 PLAYERS" -- how many players have
 *   challenges.wins       {"name","category"}: how many times the player has beaten it ("3")
 *   challenges.winners    {"name","category"}: "<player>\t<wins>" lines, most first (SWOS
 *                         2020's VIEW ONLINE STATS)
 *   challenges.ranking    {"category"}: "<player>\t<beaten>" lines, most first (its SCENARIO
 *                         COMPLETION)
 *   challenges.won        the win, sent
 *   speedrun.finished     the run into the player's record, sent
 *   speedrun.line         "WORLD RANK 3   BEST 2.21 NAME" for the result screen
 *
 * The network is SWOS-API's client (third_party/swos-api: include/united_records.h, the requests
 * and their answers); this file is the game's side -- who, a worker thread and its queue (a call
 * never waits; a read answers 1 until its answer has come), and the service.
 *
 * Who: the player logged in this session (United_UserName), else -- for what is only READ -- the
 * remembered log-in name. A record is only ever SENT for a logged-in player: SWOS 2020's records
 * API takes a name and no password, so nothing goes under a name nobody logged in as. Nobody:
 * no service answers (-1), and the mods are offline.
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)

#include "mjson.h"
#include "united_api.h"
#include "united_records.h"
#include "platform_sdl.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "online-records"

static void who(char *out, uint32_t cap, int32_t mustBeLoggedIn)
{
    extern PlatformInitOptions *g_arcadeOpts;
    const char *u = United_UserName();
    if (u && u[0])
        snprintf(out, cap, "%s", u);
    else if (!mustBeLoggedIn && g_arcadeOpts && g_arcadeOpts->netplay_remember && g_arcadeOpts->netplay_name[0])
        snprintf(out, cap, "%s", g_arcadeOpts->netplay_name);
    else
        out[0] = 0;
    for (char *c = out; *c; c++)           /* (SWOS 2020 keeps names in capitals) */
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
}

/* ---- the worker and its queue ---- */

enum { J_TALLY = 1, J_WON, J_SPEEDRUN };
typedef struct {
    int32_t        kind;
    char           user[64];
    char           name[96], category[96];  /* won */
    UnitedSpeedrun run;                     /* (run.user points at user) */
} Job;

enum { kQueue = 16 };
static Job         s_queue[kQueue];
static int32_t     s_head, s_tail;
static SDL_mutex  *s_lock;
static SDL_cond   *s_wake;

/* What has come back (under s_lock). */
static int32_t              s_tallyState;   /* 0 not asked, 1 asking, 2 here, 3 failed */
static char                 s_tallyUser[64];
static UnitedChallengeTally s_tally;
static int32_t              s_speedrunState; /* 0 none, 1 working, 2 line here */
static char                 s_speedrunLine[64];

static void push(const Job *j)
{
    SDL_LockMutex(s_lock);
    if ((s_tail + 1) % kQueue != s_head) {
        s_queue[s_tail] = *j;
        s_tail = (s_tail + 1) % kQueue;
        SDL_CondSignal(s_wake);
    } else {
        fprintf(stderr, "[online-records] the queue is full: a job dropped\n");
    }
    SDL_UnlockMutex(s_lock);
}

static void run_job(Job *j)
{
    if (j->kind == J_TALLY) {
        UnitedChallengeTally t;
        const int32_t ok = UnitedRecords_ChallengeTally(j->user, &t);
        SDL_LockMutex(s_lock);
        UnitedRecords_FreeTally(&s_tally);
        s_tally = t;
        s_tallyState = ok ? 2 : 3;
        SDL_UnlockMutex(s_lock);
        fprintf(stderr, "[online-records] challenges: %s (%d player-challenge rows)\n", ok ? "here" : "not fetched",
                (int)t.numRows);
    } else if (j->kind == J_WON) {
        if (UnitedRecords_ChallengeWon(j->user, j->name, j->category))
            fprintf(stderr, "[online-records] challenge won, sent: %s (%s)\n", j->name, j->category);
    } else if (j->kind == J_SPEEDRUN) {
        char line[64];
        j->run.user = j->user;
        const int32_t ok = UnitedRecords_SpeedrunRun(&j->run, line, sizeof line);
        SDL_LockMutex(s_lock);
        snprintf(s_speedrunLine, sizeof s_speedrunLine, "%s", ok ? line : "");
        s_speedrunState = 2;
        SDL_UnlockMutex(s_lock);
        fprintf(stderr, "[online-records] speedrun %s (style %d, mode %d, difficulty %d): %s\n",
                ok ? "sent" : "NOT sent", (int)j->run.style, (int)j->run.mode, (int)j->run.difficulty, line);
    }
}

static int worker(void *unused)
{
    (void)unused;
    for (;;) {
        Job j;
        SDL_LockMutex(s_lock);
        while (s_head == s_tail)
            SDL_CondWait(s_wake, s_lock);
        j = s_queue[s_head];
        s_head = (s_head + 1) % kQueue;
        SDL_UnlockMutex(s_lock);
        run_job(&j);
    }
    return 0;
}

/* ---- the service ---- */

static void json_str(const char *args, const char *path, char *out, int32_t cap)
{
    if (mjson_get_string(args, (int)strlen(args), path, out, cap) < 0)
        out[0] = 0;
}

static long json_num(const char *args, const char *path)
{
    long v = 0;
    mjson_get_long(args, (int)strlen(args), path, &v);
    return v;
}

static int32_t json_bool(const char *args, const char *path)
{
    int v = 0;
    mjson_get_bool(args, (int)strlen(args), path, &v);
    return v;
}

static int32_t challenges_read(const char *method, const char *args, char *out, uint32_t cap)
{
    char me[64];
    const int32_t personal = !strcmp(method, "challenges.completed") || !strcmp(method, "challenges.wins");
    who(me, sizeof me, 0);
    if (personal && !me[0])
        return -1;
    SDL_LockMutex(s_lock);
    if (s_tallyState == 0 || strcmp(s_tallyUser, me) != 0) {
        Job j;
        memset(&j, 0, sizeof j);
        j.kind = J_TALLY;
        snprintf(j.user, sizeof j.user, "%s", me);
        snprintf(s_tallyUser, sizeof s_tallyUser, "%s", me);
        s_tallyState = 1;
        SDL_UnlockMutex(s_lock);
        push(&j);
        return 1;
    }
    const int32_t rc = s_tallyState == 1 ? 1 : s_tallyState == 3 ? -1 : 0;
    if (rc == 0 && out && cap) {
        char name[96], category[96], key[200];
        json_str(args, "$.name", name, (int32_t)sizeof name);
        json_str(args, "$.category", category, (int32_t)sizeof category);
        snprintf(key, sizeof key, "%s\t%s", category, name);
        out[0] = 0;
        if (!strcmp(method, "challenges.completed")) {
            UnitedRecords_TallyMine(&s_tally, out, cap);
        } else if (!strcmp(method, "challenges.wins")) {
            snprintf(out, cap, "%d", (int)UnitedRecords_TallyWins(&s_tally, key, NULL));
        } else if (!strcmp(method, "challenges.winners")) {
            UnitedRecords_TallyWinners(&s_tally, key, out, cap);
        } else if (!strcmp(method, "challenges.ranking")) {
            UnitedRecords_TallyRanking(&s_tally, category, out, cap);
        } else {
            const int32_t n = UnitedRecords_TallyPlayers(&s_tally, key);
            if (n == 0)
                snprintf(out, cap, "NOBODY HAS BEATEN IT YET");
            else
                snprintf(out, cap, "BEATEN BY %d PLAYER%s", (int)n, n == 1 ? "" : "S");
        }
    }
    SDL_UnlockMutex(s_lock);
    return rc;
}

static int32_t service(const char *method, const char *args, char *out, uint32_t cap)
{
    char me[64];
    if (!strcmp(method, "challenges.completed") || !strcmp(method, "challenges.stats") ||
        !strcmp(method, "challenges.wins") || !strcmp(method, "challenges.winners") ||
        !strcmp(method, "challenges.ranking"))
        return challenges_read(method, args, out, cap);
    if (!strcmp(method, "challenges.won")) {
        who(me, sizeof me, 1);
        if (!me[0])
            return -1;          /* (nobody logged in: nothing sent under a name) */
        Job j;
        memset(&j, 0, sizeof j);
        j.kind = J_WON;
        snprintf(j.user, sizeof j.user, "%s", me);
        json_str(args, "$.name", j.name, (int32_t)sizeof j.name);
        json_str(args, "$.category", j.category, (int32_t)sizeof j.category);
        push(&j);
        SDL_LockMutex(s_lock);  /* (here too at once: the page counts it, the list has it) */
        if (s_tallyState == 2) {
            char key[200];
            snprintf(key, sizeof key, "%s\t%s", j.category, j.name);
            UnitedRecords_TallyAddMine(&s_tally, key);
        }
        SDL_UnlockMutex(s_lock);
        return 0;
    }
    if (!strcmp(method, "speedrun.finished")) {
        who(me, sizeof me, 1);
        if (!me[0])
            return -1;
        char style[16];
        Job j;
        memset(&j, 0, sizeof j);
        j.kind = J_SPEEDRUN;
        snprintf(j.user, sizeof j.user, "%s", me);
        json_str(args, "$.style", style, (int32_t)sizeof style);
        j.run.style = UnitedRecords_Style(style);
        j.run.mode = (int32_t)json_num(args, "$.target");
        j.run.difficulty = UnitedRecords_Difficulty(json_num(args, "$.yours"), json_num(args, "$.theirs"));
        j.run.done = json_bool(args, "$.done");
        j.run.frames = (int32_t)json_num(args, "$.frames");
        j.run.ms = (int32_t)json_num(args, "$.ms");
        if (j.run.mode < 0 || j.run.mode > 7)
            return -1;
        SDL_LockMutex(s_lock);
        s_speedrunState = 1;
        s_speedrunLine[0] = 0;
        SDL_UnlockMutex(s_lock);
        push(&j);
        return 0;
    }
    if (!strcmp(method, "speedrun.line")) {
        SDL_LockMutex(s_lock);
        const int32_t state = s_speedrunState;
        if (state == 2 && out && cap)
            snprintf(out, cap, "%s", s_speedrunLine);
        SDL_UnlockMutex(s_lock);
        return state == 1 ? 1 : state == 2 && s_speedrunLine[0] ? 0 : -1;
    }
    return -1;
}

/* SWOS_RECORDS_TEST=1 (with a test log-in: SWOS_UNITED_LOGIN, SWOS_UNITED_ACCOUNTS): at the first
 * menu frame, the live servers end to end as the logged-in player, straight through SWOS-API's
 * client -- the challenges' tally, a win (HOLD ON, CLASSICS) and the tally again, then a
 * speedrun (1 GOAL, AMIGA, ADVANCED) and its line. Prints RECORDS TEST and exits. */
static void records_test(void)
{
    static int32_t done;
    char me[64], line[64];
    const char *key = "CLASSICS\tHOLD ON";
    if (done || !getenv("SWOS_RECORDS_TEST"))
        return;
    who(me, sizeof me, 1);
    if (!me[0])
        return;                             /* (the test log-in not through yet) */
    done = 1;
    UnitedChallengeTally before, after;
    const int32_t a = UnitedRecords_ChallengeTally(me, &before);
    const int32_t n0 = a ? UnitedRecords_TallyPlayers(&before, key) : -1;
    const int32_t w0 = a ? UnitedRecords_TallyWins(&before, key, NULL) : -1;
    const int32_t sent = UnitedRecords_ChallengeWon(me, "HOLD ON", "CLASSICS");
    const int32_t b = UnitedRecords_ChallengeTally(me, &after);
    const int32_t n1 = b ? UnitedRecords_TallyPlayers(&after, key) : -1;
    const int32_t w1 = b ? UnitedRecords_TallyWins(&after, key, NULL) : -1;
    const int32_t has = b && w1 == w0 + 1;
    {
        static char top[4096];
        UnitedRecords_TallyRanking(&after, "CLASSICS", top, sizeof top);
        char *nl = strchr(top, '\n');
        if (nl)
            *nl = 0;
        printf("RECORDS TEST CLASSICS' first: %s\n", top);
    }
    printf("RECORDS TEST challenges as %s: tally %s, HOLD ON beaten by %d players before (mine %d times), win %s, "
           "after: %d players (mine %d times: %s)\n", me, a ? "fetched" : "FAILED", (int)n0, (int)w0,
           sent ? "sent" : "NOT SENT", (int)n1, (int)w1, has ? "one more" : "NOT ONE MORE");
    UnitedRecords_FreeTally(&before);
    UnitedRecords_FreeTally(&after);
    UnitedSpeedrun run;
    memset(&run, 0, sizeof run);
    run.user = me;
    run.style = UnitedRecords_Style("AMIGA");
    run.mode = 0;
    run.difficulty = 2;
    run.done = 1;
    run.frames = 1500;
    run.ms = 30000;
    const int32_t ok = UnitedRecords_SpeedrunRun(&run, line, sizeof line);
    printf("RECORDS TEST speedrun as %s: %s, line \"%s\"\n", me, ok ? "sent" : "NOT SENT", line);
    printf("RECORDS TEST %s\n", a && sent && b && has && ok ? "PASS" : "FAIL");
    fflush(stdout);
    exit(a && sent && b && has && ok ? 0 : 1);
}

static void records_init(void)
{
    SDL_Thread *thread;
    s_lock = SDL_CreateMutex();
    s_wake = SDL_CreateCond();
    thread = s_lock && s_wake ? SDL_CreateThread(worker, "online-records", NULL) : NULL;
    if (!thread) {
        fprintf(stderr, "[online-records] no worker thread: offline\n");
        return;
    }
    SDL_DetachThread(thread);
    ModApi_RegisterService(SLUG, "online-records", service);
}

const ModDef ModDef_onlineRecords = {
    .slug = SLUG,
    .init = records_init,
    .menuDrawn = records_test,
};

#elif defined(SWOS_SDL_BUILD)
/* (an SDL build without HTTPS -- the web's, a phone's: no records online, the mods offline) */
const ModDef ModDef_onlineRecords = { .slug = "online-records" };
#endif
