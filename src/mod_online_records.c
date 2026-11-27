/*
 * mod_online_records.c -- SWOS United's online records (fork mod "online-records"): the
 * challenges' and speedruns' records online, as SWOS 2020 kept them. The challenges and speedrun
 * mods are OpenSWOS's, shared; this mod offers the service they ask, "online-records" (OpenSWOS
 * docs/MODS.md §33). Without it they play offline, as in OpenSWOS.
 *
 *   challenges.completed  the player's beaten challenges: "<category>\t<name>" lines;
 *                         {"fresh"}: downloaded again (passed once, then polled without)
 *   challenges.stats      {"name","category"}: "BEATEN BY 12 PLAYERS" -- how many players have
 *   challenges.wins       {"name","category"}: how many times the player has beaten it ("3")
 *   challenges.winners    {"name","category"}: "<player>\t<wins>" lines, most first (SWOS
 *                         2020's VIEW ONLINE STATS)
 *   challenges.ranking    {"category"}: "<player>\t<beaten>" lines, most first (its SCENARIO
 *                         COMPLETION)
 *   challenges.won        the win of a logged-in player, sent (UNITED_WRITE_RECORDS)
 *   speedrun.finished     the run into the player's record, sent (UNITED_WRITE_SPEEDRUNS)
 *   custom.share          {"file","name", its texts}: the player's challenge uploaded (SWOS 2020's
 *                         upload_scn.php, upload_scenario_info) from the challenges mod's share.scn
 *   custom.unshare        {"file","name"}: the player's own removed from the site
 * A win sent is also posted to Discord (the discord2020 mod's "discord" service, SWOS 2020's
 * live_stats_scenario).
 *   speedrun.line         "WORLD RANK 3   BEST 2.21 NAME" for the result screen
 *   speedrun.board        {"style","target"}: the target's leaderboard, its five difficulties
 *   speedrun.mine         {"style"}: the player's records at that style
 *   speedrun.news         {}: the latest runs
 *                         (SWOS 2020's game_style numbers; the lines are united_records.h's)
 *   custom.list           {"by_name"}: the shared custom challenges' files (SWOS 2020's .SCN)
 *   custom.info           {}: their texts (nickname, time, start, et/pen, win, level)
 *   custom.download       {"file"}: the file, saved as the challenges mod's download.scn
 *   galore.chart          {"month"}: SWOS Galore's chart, all time ("") or a month ("2026-10")
 *   galore.download       {"user","file"}: a highlight, saved as the galore save folder's view.HIL
 *   speedrun.replay       {"file"}: a board's run (its .RPL, from the board's last field), saved
 *                         as the speedrun2020 save folder's run.RPL
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
#include "united_scn.h"           /* United7z_FirstFile (a speedrun replay's .7z) */
#include "https_client.h"         /* sharing a custom challenge */
#include "project_identity.h"     /* PROJECT_RECORDS_API */
#include "platform_sdl.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#define SLUG "online-records"

/* A challenge won IS written, as SWOS 2020 did (insert_new_scenario_record, 2026-11-27): only a
 * match played for real (never an editor's TEST or a test run), only for a logged-in player, into
 * SWOS 2020's records (PROJECT_RECORDS_API). 0 sends none. */
#define UNITED_WRITE_RECORDS 1

/* A SPEEDRUN run IS written, as SWOS 2020 did (Playa, 2026-11-21: "you need to log in, then your
 * runs get uploaded if they were successful"): only a run that reached its target, only for a
 * player who logged in at SPEEDRUN's log-in (speedrun2020 says so: speedrun.online), into SWOS
 * 2020's records on the site -- played + 1, the last time, the best only when it is better -- with
 * its replay: the match's .SRP (SWOS 2020 sent a .7z of its .RPL; Playa, 2026-11-21: SWOS United
 * replaces it, nothing has to read the old way). */
#define UNITED_WRITE_SPEEDRUNS 1
static int32_t s_speedrunOnline;    /* SPEEDRUN was entered through LOGIN (not PLAY OFFLINE) */

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

enum { J_TALLY = 1, J_WON, J_SPEEDRUN, J_BOARD, J_MINE, J_NEWS, J_SCNLIST, J_SCNINFO, J_SCNFILE, J_GALORE, J_HIL,
       J_RPL, J_SHARE, J_UNSHARE };
/* a shared scenario's texts, as SWOS 2020 sends them (upload_scenario_info's fields, in order) */
enum { kInfoFields = 6 };
static const char *const k_infoArgs[kInfoFields] = { "$.nickname", "$.time", "$.start", "$.etpen", "$.win", "$.level" };
static const char *const k_infoParams[kInfoFields] = { "scenario_nickname_str", "initial_time_str",
    "starting_event_str", "et_pen_str", "win_msg_str", "level_str" };
typedef struct {
    int32_t        kind;
    char           user[64];
    char           name[96], category[96];  /* won */
    UnitedSpeedrun run;                     /* (run.user points at user) */
    int32_t        style, target;           /* board, mine */
    unsigned char *replay;                  /* speedrun: its .SRP (malloc'd: the worker frees it) */
    uint32_t       replaySize;
    char           replayName[96];
    int32_t        difficulty, done, total; /* won: its stars, and its category's progress */
    char           info[kInfoFields][96];   /* share: the scenario's texts (name: its file; replay: */
    char           key[128];                /* the .SCN's bytes). key: what its slot was asked for -- */
} Job;                                      /* an answer for something else since is not stored */

/* The boards' answers (under s_lock): each the last one asked for, by what was asked. */
enum { F_BOARD, F_MINE, F_NEWS, F_SCNLIST, F_SCNINFO, F_SCNFILE, F_GALORE, F_HIL, F_RPL, F_SHARE, kFetches };
typedef struct { char key[96]; int32_t state; char *text; uint32_t size; } Fetch;   /* state: 1 asking,
                                                                              * 2 here, 3 failed */
static Fetch s_fetch[kFetches];

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

/* A board's replay is this game's (".srp"), not SWOS 2020's (".rpl", a .7z on the site). */
static int32_t srp_name(const char *file)
{
    const size_t n = strlen(file);
    return n >= 4 && SDL_strcasecmp(file + n - 4, ".srp") == 0;
}

/* The run's replay, for the upload: the match's recording (LATEST.SRP, written when the match
 * ended), named as SWOS 2020 named its runs -- "<user>-2026-1121-193005-auto.srp" (the boards read
 * the date from its end). 0 none. */
static int32_t run_replay(const char *user, Job *j)
{
    char path[1024];
    FILE *f;
    long size;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    uint32_t k = 0;
    struct stat st;
    if (!tm || ModApi_LatestReplay(path, (int32_t)sizeof path) != 1)
        return 0;
    if (stat(path, &st) != 0 || st.st_mtime + 120 < now) {   /* (an older match's: not this run's) */
        fprintf(stderr, "[online-records] speedrun: the last recording is not this run's\n");
        return 0;
    }
    if (!(f = fopen(path, "rb")))
        return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    j->replay = size > 0 && size < (8L << 20) ? (unsigned char *)malloc((size_t)size) : NULL;
    if (!j->replay || fread(j->replay, 1, (size_t)size, f) != (size_t)size) {
        free(j->replay);
        j->replay = NULL;
        fclose(f);
        return 0;
    }
    fclose(f);
    j->replaySize = (uint32_t)size;
    for (const char *c = user; *c && k < 40; c++)   /* (a file name the site keeps as it is) */
        j->replayName[k++] = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                             *c == '_' ? *c : '_';
    snprintf(j->replayName + k, sizeof j->replayName - k, "-%04d-%02d%02d-%02d%02d%02d-auto.srp",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    return 1;
}

static void push(const Job *j)
{
    SDL_LockMutex(s_lock);
    if ((s_tail + 1) % kQueue != s_head) {
        s_queue[s_tail] = *j;
        s_tail = (s_tail + 1) % kQueue;
        SDL_CondSignal(s_wake);
    } else {
        fprintf(stderr, "[online-records] the queue is full: a job dropped\n");
        free(j->replay);
    }
    SDL_UnlockMutex(s_lock);
}

/* ---- a win's post, and sharing a custom challenge (SWOS 2020's) ---- */

static char s_post[2000];           /* (under s_lock) a win's Discord post, for the menus' thread */

/* SWOS 2020's postNewScenarioRecordOnDiscord: who beat what, its stars, the category's progress,
 * and the players who have beaten it -- the ten with most wins, and the player below them with
 * theirs. From the tally downloaded after the win was sent. */
static void win_post(const Job *j, const UnitedChallengeTally *t)
{
    static char rows[16384], list[65536], msg[2000];
    char key[200], name[96] = "", line[48];
    int32_t done = j->done, total = j->total, players = 0, wins = 0, mine = -1, myWins = 0;
    uint32_t n = 0;
    snprintf(key, sizeof key, "%s\t%s", j->category, j->name);
    UnitedRecords_TallyWinners(t, key, rows, sizeof rows);
    {   /* the name without what is in brackets, and without words that are numbers alone */
        char plain[96], *open, *close;
        snprintf(plain, sizeof plain, "%s", j->name);
        open = strchr(plain, '(');
        close = strrchr(plain, ')');
        if (open && close && open < close)
            memmove(open, close + 1, strlen(close + 1) + 1);
        for (char *w = strtok(plain, " "); w; w = strtok(NULL, " "))
            if (w[strspn(w, "0123456789")]) {
                const size_t at = strlen(name);
                snprintf(name + at, sizeof name - at, "%s%s", at ? " " : "", w);
            }
    }
    if (!strcmp(j->category, "CUSTOM") && UnitedRecords_ScnList(0, list, sizeof list)) {
        /* (CUSTOM's progress is over the challenges shared online, as SWOS 2020's) */
        static char beaten[65536];
        UnitedRecords_TallyMine(t, beaten, sizeof beaten);
        done = total = 0;
        for (char *f = list; f && *f;) {
            char *next = strchr(f, '\n');
            if (next)
                *next++ = 0;
            char *ext = strstr(f, ".SCN");
            if (ext) {
                char one[128];
                *ext = 0;
                snprintf(one, sizeof one, "CUSTOM\t%s\n", f);
                total++;
                done += strstr(beaten, one) != NULL;
            }
            f = next;
        }
    }
    if (done > total)
        done = total;
    n += (uint32_t)snprintf(msg + n, sizeof msg - n, "```%s BEAT %s (%s)!\r\n", j->user, name, j->category);
    n += (uint32_t)snprintf(msg + n, sizeof msg - n, "DIFFICULTY RATING: %.*s\r\n",
                            (int)(j->difficulty < 0 ? 0 : j->difficulty > 5 ? 5 : j->difficulty), "*****");
    n += (uint32_t)snprintf(msg + n, sizeof msg - n, "CATEGORY PROGRESS: %d%% (%d/%d)\r\n",
                            total ? (int)((200 * done + total) / (2 * total)) : 0, (int)done, (int)total);
    snprintf(line, sizeof line, "+------------+-----+\r\n");
    n += (uint32_t)snprintf(msg + n, sizeof msg - n, "%s| NAME       | WIN |\r\n%s", line, line);
    for (char *r = rows; r && *r;) {
        char *next = strchr(r, '\n');
        if (next)
            *next++ = 0;
        char *tab = strchr(r, '\t');
        if (tab) {
            *tab = 0;
            const int32_t w = atoi(tab + 1), me = SDL_strcasecmp(r, j->user) == 0;
            if (me) {
                mine = players;
                myWins = w;
            }
            if (players < 10 && n < sizeof msg)
                n += (uint32_t)snprintf(msg + n, sizeof msg - n, "|%c%-10.10s | %3d |\r\n", me ? '*' : ' ', r, (int)w);
            players++;
            wins += w;
        }
        r = next;
    }
    if (n >= sizeof msg)
        return;
    if (mine >= 10) {           /* (below the ten: their own row) */
        n += (uint32_t)snprintf(msg + n, sizeof msg - n, "%s...\r\n%s|*%-10.10s | %3d |\r\n%s%s", line, line, j->user,
                                (int)myWins, line, mine != players - 1 ? "...\r\n" : "");
    } else {
        n += (uint32_t)snprintf(msg + n, sizeof msg - n, "%s%s", line, players > 10 ? "...\r\n" : "");
    }
    if (n < sizeof msg)
        n += (uint32_t)snprintf(msg + n, sizeof msg - n, "%d PLAYER%s, %d WIN%s\r\n```", (int)players,
                                players >= 2 ? "S" : "", (int)wins, wins >= 2 ? "S" : "");
    if (n >= sizeof msg)
        return;
    SDL_LockMutex(s_lock);
    snprintf(s_post, sizeof s_post, "%s", msg);
    SDL_UnlockMutex(s_lock);
}

/* galoreapi `option` for a shared scenario, its parameters already escaped. 1 answered. */
static int32_t galore_call(const char *option, const char *params)
{
    static char url[2400];
    HttpsReply r;
    snprintf(url, sizeof url, "%s/galoreapi_v15.php?conn_mode=1&option=%s&%s", PROJECT_RECORDS_API, option, params);
    memset(&r, 0, sizeof r);
    Https_Request(url, NULL, &r);
    const int32_t ok = r.status == 200;
    if (!ok)
        fprintf(stderr, "[online-records] %s: %d\n", option, (int)r.status);
    Https_Free(&r);
    return ok;
}

/* the file in the site's list of shared scenarios: 1 there, 0 not, -1 no list */
static int32_t scn_listed(const char *file)
{
    static char list[1 << 18];
    char want[128];
    if (!UnitedRecords_ScnList(0, list, sizeof list))
        return -1;
    snprintf(want, sizeof want, "%s\n", file);
    for (const char *p = list; (p = strstr(p, want)) != NULL; p++)
        if (p == list || p[-1] == '\n')
            return 1;
    const size_t n = strlen(list), w = strlen(file);       /* (the last line, no '\n') */
    return n >= w && !strcmp(list + n - w, file) && (n == w || list[n - w - 1] == '\n');
}

/* SWOS 2020's uploadSCNFile and uploadScenarioInfoToServer; the list says whether it arrived */
static int32_t share_scenario(const Job *j)
{
    static char url[640], params[2048];
    char esc[300];
    HttpsReply r;
    snprintf(url, sizeof url, "%s/challenges/custom/upload_scn.php", PROJECT_RECORDS_API);
    memset(&r, 0, sizeof r);
    const int32_t sent = Https_Upload(url, "myFile", j->name, j->replay, j->replaySize, &r) == 0 && r.status == 200;
    Https_Free(&r);
    if (!sent || scn_listed(j->name) != 1)
        return 0;
    Https_Escape(j->category, esc, sizeof esc);             /* (the scenario's name) */
    size_t n = (size_t)snprintf(params, sizeof params, "scenario_name_str=%s", esc);
    for (int32_t i = 0; i < kInfoFields && n < sizeof params; i++) {
        Https_Escape(j->info[i], esc, sizeof esc);
        n += (size_t)snprintf(params + n, sizeof params - n, "&%s=%s", k_infoParams[i], esc);
    }
    if (!galore_call("upload_scenario_info", params))
        fprintf(stderr, "[online-records] %s: shared, its texts not sent\n", j->name);
    return 1;
}

/* SWOS 2020's deleteSCNFile and deleteScenarioInfoFromServer */
static int32_t unshare_scenario(const Job *j)
{
    static char params[400];
    char esc[300];
    Https_Escape(j->name, esc, sizeof esc);
    snprintf(params, sizeof params, "scn_filename=%s", esc);
    if (!galore_call("delete_scn_file", params) || scn_listed(j->name) != 0)
        return 0;
    Https_Escape(j->category, esc, sizeof esc);
    snprintf(params, sizeof params, "scenario_name_str=%s", esc);
    galore_call("delete_scenario_info", params);
    return 1;
}

/* (under s_lock) whether the slot still waits for this job's answer */
static int32_t still_wanted(int32_t slot, const Job *j)
{
    return strcmp(s_fetch[slot].key, j->key) == 0;
}

static void run_job(Job *j)
{
    if (j->kind == J_TALLY) {
        UnitedChallengeTally t;
        const int32_t ok = UnitedRecords_ChallengeTally(j->user, &t);
        const int32_t rows = t.numRows;
        SDL_LockMutex(s_lock);
        if (strcmp(j->user, s_tallyUser) == 0) {
            UnitedRecords_FreeTally(&s_tally);
            s_tally = t;
            s_tallyState = ok ? 2 : 3;
        } else {
            UnitedRecords_FreeTally(&t);    /* (another player asked since: theirs is queued) */
        }
        SDL_UnlockMutex(s_lock);
        fprintf(stderr, "[online-records] challenges: %s (%d player-challenge rows)\n", ok ? "here" : "not fetched",
                (int)rows);
    } else if (j->kind == J_WON) {
        const int32_t ok = UnitedRecords_ChallengeWon(j->user, j->name, j->category);
        fprintf(stderr, "[online-records] challenge won, %s: %s (%s)\n", ok ? "sent" : "NOT sent", j->name,
                j->category);
        if (ok) {           /* the records with it in: shown from here on, and posted */
            UnitedChallengeTally t;
            if (UnitedRecords_ChallengeTally(j->user, &t)) {
                win_post(j, &t);
                SDL_LockMutex(s_lock);
                if (strcmp(j->user, s_tallyUser) == 0 && s_tallyState != 1) {
                    UnitedRecords_FreeTally(&s_tally);
                    s_tally = t;
                    s_tallyState = 2;
                } else {
                    UnitedRecords_FreeTally(&t);
                }
                SDL_UnlockMutex(s_lock);
            }
        }
    } else if (j->kind == J_SHARE || j->kind == J_UNSHARE) {
        const int32_t ok = j->kind == J_SHARE ? share_scenario(j) : unshare_scenario(j);
        free(j->replay);
        j->replay = NULL;
        SDL_LockMutex(s_lock);
        if (still_wanted(F_SHARE, j))
            s_fetch[F_SHARE].state = ok ? 2 : 3;
        s_fetch[F_SCNLIST].state = s_fetch[F_SCNINFO].state = 0;     /* (asked again) */
        SDL_UnlockMutex(s_lock);
        fprintf(stderr, "[online-records] scenario %s%s: %s\n", j->kind == J_SHARE ? "shared" : "unshared",
                ok ? "" : " FAILED", j->name);
    } else if (j->kind == J_BOARD || j->kind == J_MINE || j->kind == J_NEWS) {
        const int32_t slot = j->kind == J_BOARD ? F_BOARD : j->kind == J_MINE ? F_MINE : F_NEWS;
        const uint32_t cap = 65536;
        char *text = (char *)malloc(cap);
        const int32_t ok = text && (j->kind == J_BOARD ? UnitedRecords_SpeedrunBoard(j->style, j->target, text, cap)
                                    : j->kind == J_MINE ? UnitedRecords_SpeedrunMine(j->user, j->style, text, cap)
                                    : UnitedRecords_SpeedrunNews(text, cap));
        SDL_LockMutex(s_lock);
        const int32_t wanted = still_wanted(slot, j);
        if (wanted) {
            free(s_fetch[slot].text);
            s_fetch[slot].text = ok ? text : NULL;
            s_fetch[slot].state = ok ? 2 : 3;
        }
        SDL_UnlockMutex(s_lock);
        if (!ok || !wanted)
            free(text);
    } else if (j->kind == J_SCNLIST || j->kind == J_SCNINFO) {
        const int32_t slot = j->kind == J_SCNLIST ? F_SCNLIST : F_SCNINFO;
        const uint32_t cap = 1 << 18;
        char *text = (char *)malloc(cap);
        const int32_t ok = text && (j->kind == J_SCNLIST ? UnitedRecords_ScnList(j->target, text, cap)
                                                         : UnitedRecords_ScnInfo(text, cap));
        SDL_LockMutex(s_lock);
        free(s_fetch[slot].text);
        s_fetch[slot].text = ok ? text : NULL;
        s_fetch[slot].state = ok ? 2 : 3;
        SDL_UnlockMutex(s_lock);
        if (!ok)
            free(text);
    } else if (j->kind == J_GALORE) {
        const uint32_t cap = 1 << 18;
        char *text = (char *)malloc(cap);
        const int32_t ok = text && UnitedRecords_GaloreChart(j->category, text, cap);
        SDL_LockMutex(s_lock);
        free(s_fetch[F_GALORE].text);
        s_fetch[F_GALORE].text = ok ? text : NULL;
        s_fetch[F_GALORE].state = ok ? 2 : 3;
        SDL_UnlockMutex(s_lock);
        if (!ok)
            free(text);
    } else if (j->kind == J_HIL) {
        unsigned char *data = NULL;
        uint32_t size = 0;
        const int32_t ok = UnitedRecords_GaloreDownload(j->user, j->name, &data, &size);
        SDL_LockMutex(s_lock);
        free(s_fetch[F_HIL].text);
        s_fetch[F_HIL].text = ok ? (char *)data : NULL;
        s_fetch[F_HIL].size = ok ? size : 0;
        s_fetch[F_HIL].state = ok ? 2 : 3;
        SDL_UnlockMutex(s_lock);
        if (!ok)
            free(data);
        fprintf(stderr, "[online-records] highlight %s/%s: %s (%u bytes)\n", j->user, j->name,
                ok ? "downloaded" : "NOT downloaded", (unsigned)size);
    } else if (j->kind == J_RPL) {
        unsigned char *packed = NULL, *data = NULL;
        uint32_t packedSize = 0, size = 0;
        int32_t ok;
        if (srp_name(j->name)) {        /* (this game's: as it was uploaded) */
            ok = UnitedRecords_SpeedrunReplay(j->name, &data, &size);
        } else {
            ok = UnitedRecords_SpeedrunReplay(j->name, &packed, &packedSize) &&
                 United7z_FirstFile(packed, packedSize, ".rpl", &data, &size);
            free(packed);
        }
        SDL_LockMutex(s_lock);
        free(s_fetch[F_RPL].text);
        s_fetch[F_RPL].text = ok ? (char *)data : NULL;
        s_fetch[F_RPL].size = ok ? size : 0;
        s_fetch[F_RPL].state = ok ? 2 : 3;
        SDL_UnlockMutex(s_lock);
        if (!ok)
            free(data);
        fprintf(stderr, "[online-records] speedrun replay %s: %s (%u bytes)\n", j->name,
                ok ? "downloaded" : "NOT downloaded", (unsigned)size);
    } else if (j->kind == J_SCNFILE) {
        unsigned char *data = NULL;
        uint32_t size = 0;
        const int32_t ok = UnitedRecords_ScnDownload(j->name, &data, &size);
        SDL_LockMutex(s_lock);
        const int32_t wanted = still_wanted(F_SCNFILE, j);
        if (wanted) {
            free(s_fetch[F_SCNFILE].text);
            s_fetch[F_SCNFILE].text = ok ? (char *)data : NULL;
            s_fetch[F_SCNFILE].size = ok ? size : 0;
            s_fetch[F_SCNFILE].state = ok ? 2 : 3;
        }
        SDL_UnlockMutex(s_lock);
        if (!ok || !wanted)
            free(data);
        fprintf(stderr, "[online-records] %s: %s (%u bytes)\n", j->name, ok ? "downloaded" : "NOT downloaded",
                (unsigned)size);
    } else if (j->kind == J_SPEEDRUN) {
        char line[64];
        j->run.user = j->user;
        j->run.replay = j->replay;
        j->run.replaySize = j->replaySize;
        j->run.replayName = j->replayName;
        const int32_t ok = UnitedRecords_SpeedrunRun(&j->run, line, sizeof line);
        free(j->replay);
        j->replay = NULL;
        SDL_LockMutex(s_lock);
        snprintf(s_speedrunLine, sizeof s_speedrunLine, "%s", ok ? line : "");
        s_speedrunState = 2;
        SDL_UnlockMutex(s_lock);
        fprintf(stderr, "[online-records] speedrun %s (style %d, mode %d, difficulty %d, replay %s, %u bytes): %s\n",
                ok ? "sent" : "NOT sent", (int)j->run.style, (int)j->run.mode, (int)j->run.difficulty,
                j->replayName[0] ? j->replayName : "none", (unsigned)j->replaySize, line);
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
    const int32_t fresh = json_bool(args, "$.fresh");
    if (s_tallyState == 0 || strcmp(s_tallyUser, me) != 0 || (fresh && s_tallyState != 1)) {
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

/* A board: asked once for what is asked (the key), answered from then on; "fresh" asks again. */
static int32_t fetch(int32_t slot, int32_t kind, const char *key, const Job *job, int32_t fresh, char *out,
                     uint32_t cap)
{
    SDL_LockMutex(s_lock);
    Fetch *f = &s_fetch[slot];
    if (fresh || strcmp(f->key, key) != 0 || f->state == 0) {
        Job j = *job;
        j.kind = kind;
        snprintf(f->key, sizeof f->key, "%s", key);
        snprintf(j.key, sizeof j.key, "%s", f->key);
        f->state = 1;
        SDL_UnlockMutex(s_lock);
        push(&j);
        return 1;
    }
    const int32_t rc = f->state == 1 ? 1 : f->state == 3 ? -1 : 0;
    if (rc == 0 && out && cap)
        snprintf(out, cap, "%s", f->text ? f->text : "");
    SDL_UnlockMutex(s_lock);
    return rc;
}

static int32_t service(const char *method, const char *args, char *out, uint32_t cap)
{
    char me[64];
    if (!strcmp(method, "custom.share") || !strcmp(method, "custom.unshare")) {
        /* {"file", "name", and for share its texts}: the logged-in player's own only ("... BY
         * <USER>.SCN"); share takes the challenges mod's share.scn. 1 while at it, 0 done, -1 not
         * (poll with the same call until it is not 1) */
        const int32_t share = !strcmp(method, "custom.share");
        Job j;
        char by[80], key[128];
        memset(&j, 0, sizeof j);
        json_str(args, "$.file", j.name, (int32_t)sizeof j.name);
        json_str(args, "$.name", j.category, (int32_t)sizeof j.category);
        who(me, sizeof me, 1);
        const size_t n = strlen(j.name);
        if (!me[0] || n < 5 || strcmp(j.name + n - 4, ".SCN") != 0 || strpbrk(j.name, "/\\\"\r\n") ||
            strstr(j.name, ".."))
            return -1;
        for (char *c = me; *c; c++)             /* (as in a file's name) */
            if (strchr("/\\<>:\"|?*", *c))
                *c = '-';
        snprintf(by, sizeof by, " BY %s.SCN", me);
        if (n < strlen(by) || SDL_strcasecmp(j.name + n - strlen(by), by) != 0)
            return -1;
        /* ("seq": each time the player asks is its own -- an old answer never stands for it) */
        snprintf(key, sizeof key, "%d/%d/%s", (int)share, (int)json_num(args, "$.seq"), j.name);
        SDL_LockMutex(s_lock);
        Fetch *f = &s_fetch[F_SHARE];
        if (strcmp(f->key, key) != 0 || f->state == 0) {
            snprintf(f->key, sizeof f->key, "%s", key);
            snprintf(j.key, sizeof j.key, "%s", f->key);
            f->state = 1;
            SDL_UnlockMutex(s_lock);
            if (share) {
                uint32_t size = 0;
                j.replay = (unsigned char *)ModApi_LoadFile("challenges", "share.scn", &size);
                j.replaySize = size;
                for (int32_t i = 0; i < kInfoFields; i++)
                    json_str(args, k_infoArgs[i], j.info[i], (int32_t)sizeof j.info[0]);
                if (!j.replay || !size) {
                    free(j.replay);
                    SDL_LockMutex(s_lock);
                    f->state = 0;
                    SDL_UnlockMutex(s_lock);
                    return -1;
                }
            }
            j.kind = share ? J_SHARE : J_UNSHARE;
            push(&j);
            return 1;
        }
        const int32_t rc = f->state == 1 ? 1 : f->state == 3 ? -1 : 0;
        if (rc != 1) {                          /* (answered once: asked again, it is done again) */
            f->state = 0;
            f->key[0] = 0;
        }
        SDL_UnlockMutex(s_lock);
        return rc;
    }
    if (!strcmp(method, "custom.list") || !strcmp(method, "custom.info")) {
        Job j;
        memset(&j, 0, sizeof j);
        const int32_t byName = json_bool(args, "$.by_name");
        j.target = byName;
        if (!strcmp(method, "custom.info"))
            return fetch(F_SCNINFO, J_SCNINFO, "info", &j, json_bool(args, "$.fresh"), out, cap);
        return fetch(F_SCNLIST, J_SCNLIST, byName ? "name" : "date", &j, json_bool(args, "$.fresh"), out, cap);
    }
    if (!strcmp(method, "galore.chart")) {
        Job j;
        char key[32];
        memset(&j, 0, sizeof j);
        json_str(args, "$.month", j.category, 8);
        snprintf(key, sizeof key, "chart/%s", j.category);
        return fetch(F_GALORE, J_GALORE, key, &j, json_bool(args, "$.fresh"), out, cap);
    }
    if (!strcmp(method, "speedrun.replay")) {
        Job j;
        memset(&j, 0, sizeof j);
        json_str(args, "$.file", j.name, (int32_t)sizeof j.name);
        if (!j.name[0] || strchr(j.name, '/') || strchr(j.name, '\\') || strstr(j.name, ".."))
            return -1;
        SDL_LockMutex(s_lock);
        Fetch *f = &s_fetch[F_RPL];
        if (strcmp(f->key, j.name) != 0 || f->state == 0) {
            snprintf(f->key, sizeof f->key, "%s", j.name);
            f->state = 1;
            SDL_UnlockMutex(s_lock);
            j.kind = J_RPL;
            push(&j);
            return 1;
        }
        int32_t rc = f->state == 1 ? 1 : f->state == 3 ? -1 : 0;
        if (rc == 0)        /* (run.SRP: SWOS United's; run.RPL: SWOS 2020's) */
            rc = ModApi_SaveFile("speedrun2020", srp_name(j.name) ? "run.SRP" : "run.RPL", f->text, f->size) == 0
                 ? 0 : -1;
        if (rc == -1)
            f->state = 0;               /* (asked again, it is fetched again) */
        SDL_UnlockMutex(s_lock);
        return rc;
    }
    if (!strcmp(method, "galore.download")) {
        Job j;
        char key[96];
        memset(&j, 0, sizeof j);
        json_str(args, "$.user", j.user, (int32_t)sizeof j.user);
        json_str(args, "$.file", j.name, (int32_t)sizeof j.name);
        if (!j.user[0] || !j.name[0] || strchr(j.name, '/') || strchr(j.user, '/') || strstr(j.name, "..") ||
            strstr(j.user, ".."))
            return -1;
        snprintf(key, sizeof key, "%s/%s", j.user, j.name);
        SDL_LockMutex(s_lock);
        Fetch *f = &s_fetch[F_HIL];
        if (strcmp(f->key, key) != 0 || f->state == 0) {
            snprintf(f->key, sizeof f->key, "%s", key);
            f->state = 1;
            SDL_UnlockMutex(s_lock);
            j.kind = J_HIL;
            push(&j);
            return 1;
        }
        int32_t rc = f->state == 1 ? 1 : f->state == 3 ? -1 : 0;
        if (rc == 0)
            rc = ModApi_SaveFile("galore", "view.HIL", f->text, f->size) == 0 ? 0 : -1;
        if (rc != 1) {
            f->state = 0;
            f->key[0] = 0;
        }
        SDL_UnlockMutex(s_lock);
        if (rc == 0 && out && cap)
            snprintf(out, cap, "view.HIL");
        return rc;
    }
    if (!strcmp(method, "custom.download")) {
        Job j;
        char key[96];
        memset(&j, 0, sizeof j);
        json_str(args, "$.file", j.name, (int32_t)sizeof j.name);
        if (!j.name[0] || strchr(j.name, '/') || strchr(j.name, '\\') || strstr(j.name, ".."))
            return -1;
        snprintf(key, sizeof key, "%s", j.name);
        SDL_LockMutex(s_lock);
        Fetch *f = &s_fetch[F_SCNFILE];
        if (strcmp(f->key, key) != 0 || f->state == 0) {
            snprintf(f->key, sizeof f->key, "%s", key);
            snprintf(j.key, sizeof j.key, "%s", f->key);
            f->state = 1;
            SDL_UnlockMutex(s_lock);
            j.kind = J_SCNFILE;
            push(&j);
            return 1;
        }
        int32_t rc = f->state == 1 ? 1 : f->state == 3 ? -1 : 0;
        if (rc == 0) {          /* (here: saved from this thread, for the challenges' screens) */
            rc = ModApi_SaveFile("challenges", "download.scn", f->text, f->size) == 0 ? 0 : -1;
            f->state = 0;       /* (asked again: fetched again) */
            f->key[0] = 0;
        } else if (rc == -1) {
            f->state = 0;
            f->key[0] = 0;
        }
        SDL_UnlockMutex(s_lock);
        if (rc == 0 && out && cap)
            snprintf(out, cap, "download.scn");
        return rc;
    }
    if (!strcmp(method, "speedrun.board") || !strcmp(method, "speedrun.mine") || !strcmp(method, "speedrun.news")) {
        Job j;
        char key[96];
        memset(&j, 0, sizeof j);
        j.style = (int32_t)json_num(args, "$.style");
        j.target = (int32_t)json_num(args, "$.target");
        const int32_t fresh = json_bool(args, "$.fresh");
        if (!strcmp(method, "speedrun.news"))
            return fetch(F_NEWS, J_NEWS, "news", &j, fresh, out, cap);
        if (!strcmp(method, "speedrun.board")) {
            snprintf(key, sizeof key, "%d/%d", (int)j.style, (int)j.target);
            return fetch(F_BOARD, J_BOARD, key, &j, fresh, out, cap);
        }
        who(me, sizeof me, 0);
        if (!me[0])
            return -1;          /* (whose records: nobody's) */
        snprintf(j.user, sizeof j.user, "%s", me);
        snprintf(key, sizeof key, "%s/%d", me, (int)j.style);
        return fetch(F_MINE, J_MINE, key, &j, fresh, out, cap);
    }
    if (!strcmp(method, "challenges.completed") || !strcmp(method, "challenges.stats") ||
        !strcmp(method, "challenges.wins") || !strcmp(method, "challenges.winners") ||
        !strcmp(method, "challenges.ranking"))
        return challenges_read(method, args, out, cap);
    if (!strcmp(method, "challenges.won")) {
        if (!UNITED_WRITE_RECORDS)
            return -1;          /* (not sent: see UNITED_WRITE_RECORDS) */
        who(me, sizeof me, 1);
        if (!me[0])
            return -1;          /* (nobody logged in: nothing sent under a name) */
        Job j;
        memset(&j, 0, sizeof j);
        j.kind = J_WON;
        snprintf(j.user, sizeof j.user, "%s", me);
        json_str(args, "$.name", j.name, (int32_t)sizeof j.name);
        json_str(args, "$.category", j.category, (int32_t)sizeof j.category);
        j.difficulty = (int32_t)json_num(args, "$.difficulty");
        j.done = (int32_t)json_num(args, "$.done");
        j.total = (int32_t)json_num(args, "$.total");
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
    if (!strcmp(method, "speedrun.online")) {
        s_speedrunOnline = json_bool(args, "$.on");
        return 0;
    }
    if (!strcmp(method, "speedrun.finished")) {
        SDL_LockMutex(s_lock);  /* (a run not sent has no line: not the last one's) */
        if (s_speedrunState != 1)
            s_speedrunState = 0;
        SDL_UnlockMutex(s_lock);
        if (!UNITED_WRITE_SPEEDRUNS || !s_speedrunOnline || !json_bool(args, "$.done"))
            return -1;          /* (not sent: offline, or the target not reached -- SWOS 2020's
                                 *  g_SpeedrunResult == 1 only) */
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
        {   /* SWOS 2020's preconfigured teams' difficulty, else the teams' skill points' */
            long preset = -1;
            mjson_get_long(args, (int)strlen(args), "$.difficulty", &preset);
            j.run.difficulty = preset >= 0 && preset <= 4 ? (int32_t)preset
                             : UnitedRecords_Difficulty(json_num(args, "$.yours"), json_num(args, "$.theirs"));
        }
        if (j.run.mode == 5 || j.run.mode == 7)
            j.run.difficulty = 0;   /* (the cards have one board: the site's rows are difficulty 0) */
        j.run.done = json_bool(args, "$.done");
        j.run.frames = (int32_t)json_num(args, "$.frames");
        j.run.ms = (int32_t)json_num(args, "$.ms");
        if (j.run.mode < 0 || j.run.mode > 7 || j.run.frames <= 0)
            return -1;
        if (!run_replay(me, &j))
            fprintf(stderr, "[online-records] speedrun: no replay to send with it\n");
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
        else if (state == 1 && out && cap)  /* (SWOS 2020's "RECORD ... WILL BE UPLOADED") */
            snprintf(out, cap, "UPLOADING THE RECORD...");
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
    if (!UNITED_WRITE_RECORDS) {    /* (writes off: the reads alone) */
        UnitedChallengeTally t;
        static char board[1 << 16];
        const int32_t a = UnitedRecords_ChallengeTally(me, &t);
        const int32_t b = UnitedRecords_SpeedrunBoard(UnitedRecords_Style("AMIGA"), 0, board, sizeof board);
        printf("RECORDS TEST (reads only) as %s: tally %s (%d rows), board %s\n", me, a ? "fetched" : "FAILED",
               (int)t.numRows, b ? "fetched" : "FAILED");
        UnitedRecords_FreeTally(&t);
        printf("RECORDS TEST %s\n", a && b ? "PASS" : "FAIL");
        fflush(stdout);
        exit(a && b ? 0 : 1);
    }
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

/* Each menu frame: a win's post, written by the worker, handed to the discord2020 mod here (the
 * menus' thread: services are called from it); then the test hook. */
static void records_menu(void)
{
    static char post[2000], args[4400];
    post[0] = 0;
    if (s_lock) {
        SDL_LockMutex(s_lock);
        snprintf(post, sizeof post, "%s", s_post);
        s_post[0] = 0;
        SDL_UnlockMutex(s_lock);
    }
    if (post[0]) {      /* (its lines kept: ModApi_JsonEscape would drop them) */
        size_t n = (size_t)snprintf(args, sizeof args, "{\"type\":\"live_stats_scenario\",\"content\":\"");
        for (const char *c = post; *c && n + 8 < sizeof args; c++) {
            if (*c == '\n') {
                args[n++] = '\\';
                args[n++] = 'n';
            } else if ((unsigned char)*c >= 32) {
                if (*c == '"' || *c == '\\')
                    args[n++] = '\\';
                args[n++] = *c;
            }
        }
        snprintf(args + n, sizeof args - n, "\"}");
        ModApi_CallService("discord", "post", args, NULL, 0);
    }
    records_test();
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
    .menuDrawn = records_menu,
};

#elif defined(SWOS_SDL_BUILD)
/* (an SDL build without HTTPS -- the web's, a phone's: no records online, the mods offline) */
const ModDef ModDef_onlineRecords = { .slug = "online-records" };
#endif
