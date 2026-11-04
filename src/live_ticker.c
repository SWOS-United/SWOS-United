/*
 * live_ticker.c -- an online match on Discord: SWOS 2020's LIVE TICKERS and LIVE SCORE BOARDS
 * (include/live_ticker.h). SWOS 2020's are LiveTicker::poll(), livescoreWebhook() and
 * postToDiscordProxy() (its src/mpgame.cpp, src/util/util.cpp); the lines here are its lines, its
 * emoji (its Discord server's own) and its channels.
 */
#include "live_ticker.h"

#ifdef SWOS_SDL_BUILD

#include "project_identity.h"
#include "netplay_lobby.h"
#include "dos_sound.h"
#include "dos_mode.h"
#include "game_types.h"
#include "mm_client.h"
#ifdef SWOS_HTTPS
#include "https_client.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

extern TeamGeneralInfo _topTeamData, _bottomTeamData;
extern TeamGeneralInfo *_lastTeamPlayed;
extern Sprite   *_lastPlayerPlayed, *_lastKeeperPlayed, *_bookedPlayer;
extern Sprite    _goalie1Sprite, _goalie2Sprite;
extern uint16_t  _ingameTeamPlayerOffsets[];
extern uint16_t  _statsTeam1Goals, _statsTeam2Goals, _team1PenaltyGoals, _team2PenaltyGoals;
extern uint16_t  _gameState, _penalty, _cameraDirection, _pitchNumberAndType;
extern uint16_t  _penaltiesState[1];
extern uint8_t   _gameTime[];

/* SWOS 2020's Discord server's emoji. */
#define EMO_PLAY    "<:play:368864435189710848>"
#define EMO_TICK    "<:tick:284642999319920641>"
#define EMO_BALL    "<:ball:1049883986450731080>"
#define EMO_YELLOW  "<:yellow:1049885198982058004>"
#define EMO_RED     "<:red:1049884826141982830>"
static const char *const k_styleEmoji[GAMEPLAY_STYLES] = {
    "<:amiga:1050533088062607360>", "<:dos:1050532548519931915>", "<:xbox:1050546296513110046>",
};
static const char *const k_injuryEmoji[8] = {
    "", "<:injury1:1049879183343489075>", "<:injury2:1049879642179375185>",
    "<:injury3:1049880059885920256>", "<:injury4:1049880468243349574>",
    "<:injury5:1049880800201551934>", "<:injury6:1049881107706957824>",
    "<:injury7:1049881644808556574>",
};
static const char *const k_pitchNames[7] = { "FROZEN", "MUDDY", "WET", "SOFT", "NORMAL", "DRY", "HARD" };

/* ---- the settings ---- */

static int32_t s_cfgOn = 1;
static char    s_webhook[256];

void LiveTicker_Configure(int32_t on, const char *testWebhook)
{
    const char *env = getenv("SWOS_DISCORD_WEBHOOK");
    s_cfgOn = on;
    snprintf(s_webhook, sizeof s_webhook, "%s", env && env[0] ? env : testWebhook ? testWebhook : "");
    env = getenv("SWOS_DISCORD");
    if (env && env[0])
        s_cfgOn = strcmp(env, "0") != 0;
}

/* A matchmaker on this machine or the local network: a test, not a match for Discord. */
static int32_t local_server(void)
{
    const char *h = MmClient_DefaultHost();
    int32_t b = 0;
    if (!h || !h[0])
        return 0;
    if (strcmp(h, "localhost") == 0 || strncmp(h, "127.", 4) == 0 || strncmp(h, "10.", 3) == 0 ||
        strncmp(h, "192.168.", 8) == 0 || strcmp(h, "::1") == 0)
        return 1;
    if (sscanf(h, "172.%d.", &b) == 1 && b >= 16 && b <= 31)
        return 1;
    return 0;
}

/* ---- posting, on a thread of its own ---- */

typedef struct Post {
    struct Post *next;
    char         type[32];
    char        *text;
} Post;

static SDL_mutex  *s_qLock;
static SDL_cond   *s_qCond;
static Post       *s_qHead, *s_qTail;
static SDL_Thread *s_worker;

#ifdef SWOS_HTTPS
static char *json_content(const char *prefix, const char *text)
{
    size_t n = strlen(prefix) + strlen(text), o = 0;
    char *out = (char *)malloc(n * 6 + 32);
    const char *parts[2] = { prefix, text };
    int32_t k;
    if (!out)
        return NULL;
    o += (size_t)sprintf(out, "{\"content\":\"");
    for (k = 0; k < 2; k++) {
        const unsigned char *p;
        for (p = (const unsigned char *)parts[k]; *p; p++) {
            if (*p == '"' || *p == '\\') {
                out[o++] = '\\';
                out[o++] = (char)*p;
            } else if (*p == '\n') {
                out[o++] = '\\';
                out[o++] = 'n';
            } else if (*p < 0x20) {
                o += (size_t)sprintf(out + o, "\\u%04x", *p);
            } else {
                out[o++] = (char)*p;
            }
        }
    }
    strcpy(out + o, "\"}");
    return out;
}

static void send_post(const Post *p)
{
    char *body = NULL;
    const char *url;
    HttpsReply reply;
    int32_t attempt;

    if (s_webhook[0]) {             /* a test channel: every post, with the channel it is for */
        char prefix[48];
        snprintf(prefix, sizeof prefix, "`%s` ", p->type);
        body = json_content(prefix, p->text);
        url = s_webhook;
    } else {
        size_t n = strlen(p->text) * 3 + 1;
        char *esc = (char *)malloc(n);
        body = (char *)malloc(n + 64);
        if (esc && body) {
            Https_Escape(p->text, esc, (uint32_t)n);
            sprintf(body, "type=%s&content=%s", p->type, esc);
        }
        free(esc);
        url = PROJECT_DISCORD_PROXY;
    }
    if (!body)
        return;
    for (attempt = 0; attempt < 3; attempt++) {
        memset(&reply, 0, sizeof reply);
        Https_Request(url, body, &reply);
        Https_Free(&reply);
        if (reply.status != 429)
            break;
        SDL_Delay(2000);            /* Discord's rate limit: try again */
    }
    /* (never the URL: a test webhook is a secret) */
    fprintf(stderr, "[ticker] posted to %s: %d\n", p->type, (int)reply.status);
    free(body);
    SDL_Delay(300);                 /* (a webhook takes about five posts in two seconds) */
}
#endif

static int worker_main(void *unused)
{
    (void)unused;
    for (;;) {
        Post *p;
        SDL_LockMutex(s_qLock);
        while (!s_qHead)
            SDL_CondWait(s_qCond, s_qLock);
        p = s_qHead;
        s_qHead = p->next;
        if (!s_qHead)
            s_qTail = NULL;
        SDL_UnlockMutex(s_qLock);
#ifdef SWOS_HTTPS
        send_post(p);
#endif
        free(p->text);
        free(p);
    }
    return 0;
}

static void post(const char *type, const char *text)
{
    Post *p;
    fprintf(stderr, "[ticker] %s: %s\n", type, text);
#ifndef SWOS_HTTPS
    return;
#endif
    if (!s_qLock) {
        s_qLock = SDL_CreateMutex();
        s_qCond = SDL_CreateCond();
    }
    if (!s_worker) {
        s_worker = SDL_CreateThread(worker_main, "discord", NULL);
        if (!s_worker)
            return;
        SDL_DetachThread(s_worker);
    }
    p = (Post *)calloc(1, sizeof *p);
    if (!p || !(p->text = strdup(text))) {
        free(p);
        return;
    }
    snprintf(p->type, sizeof p->type, "%s", type);
    SDL_LockMutex(s_qLock);
    if (s_qTail)
        s_qTail->next = p;
    else
        s_qHead = p;
    s_qTail = p;
    SDL_CondSignal(s_qCond);
    SDL_UnlockMutex(s_qLock);
}

/* ---- the match ---- */

static LiveTickerMatch s_m;
static int32_t s_enabled;               /* this match goes to Discord */
static int32_t s_collect;               /* ... and this machine posts its moments (the host) */
static int32_t s_frame;                 /* the frame being played */
static int32_t s_confirmed;
static int32_t s_injuries[2];

/* A moment reported in a frame that is not confirmed yet. */
#define PENDING_MAX 48
typedef struct {
    int32_t frame;
    int32_t injuryTeam;                 /* an injury: its team, for the score board's count */
    char    text[320];
} Pending;
static Pending s_pending[PENDING_MAX];
static int32_t s_pendingCount;

/* The state at the start of each frame, for what is seen from one frame to the next. */
#define SNAP_FRAMES 256
typedef struct { int32_t frame; uint16_t gameState; int16_t minute; uint8_t shootout; } Snap;
static Snap s_snap[SNAP_FRAMES];

/* A goal, between its DOS_EV_GOAL and the DOS_EV_SCORE after it. */
static struct { int32_t have, team, own, shootout; char name[24]; } s_goal;
static int32_t s_penaltyTeam;           /* the last penalty's, for a miss */

static const char *ticker_type(void)
{
    return s_m.mode == LOBBY_MODE_RANKED ? "live_ticker_ranked"
         : s_m.mode == LOBBY_MODE_LEAGUE ? "live_ticker_tournament" : "live_ticker_friendly";
}

static const char *board_type(void)
{
    return s_m.mode == LOBBY_MODE_RANKED ? "live_scoreboard_ranked"
         : s_m.mode == LOBBY_MODE_LEAGUE ? "live_scoreboard_tourney" : "live_scoreboard_friendly";
}

static int32_t minute(void)
{
    return _gameTime[1] * 100 + _gameTime[2] * 10 + _gameTime[3];
}

static void copy_name(char *out, uint32_t cap, const int8_t *in, uint32_t inCap)
{
    uint32_t i = 0, o = 0;
    while (i < inCap && in[i] == ' ')
        i++;
    for (; i < inCap && in[i] && o + 1 < cap; i++)
        out[o++] = (char)in[i];
    while (o && out[o - 1] == ' ')
        o--;
    out[o] = 0;
}

static TeamGeneralInfo *team_info(int32_t team)
{
    return _topTeamData.teamNumber == team ? &_topTeamData : &_bottomTeamData;
}

int32_t LiveTicker_TeamOf(const void *teamGame)
{
    return (const void *)_topTeamData.inGameTeamPtr == teamGame ? _topTeamData.teamNumber : _bottomTeamData.teamNumber;
}

static void sprite_name(const Sprite *s, char *out, uint32_t cap)
{
    const PlayerInfo *pi;
    if (!s || s->teamNumber < 1 || s->teamNumber > 2 || s->playerOrdinal < 1 || s->playerOrdinal > 16) {
        snprintf(out, cap, "?");
        return;
    }
    pi = (const PlayerInfo *)((const uint8_t *)team_info(s->teamNumber)->inGameTeamPtr +
                              _ingameTeamPlayerOffsets[s->playerOrdinal - 1] + 42);
    copy_name(out, cap, pi->shortName, sizeof pi->shortName);
}

static const char *team_name(int32_t team)
{
    return team == 1 || team == 2 ? s_m.team[team - 1] : "?";
}

/* "**1 - 0**": the ninety minutes and extra time (the shoot-out's are apart). */
static int32_t goals(int32_t team)
{
    return team == 1 ? (int32_t)_statsTeam1Goals - _team1PenaltyGoals : (int32_t)_statsTeam2Goals - _team2PenaltyGoals;
}

/* "`[A vs. B]`" */
static const char *vs(void)
{
    static char text[64];
    snprintf(text, sizeof text, "`[%s vs. %s]`", s_m.name[0], s_m.name[1]);
    return text;
}

static void flag_emoji(const char *iso2, char *out, uint32_t cap)
{
    if (strcmp(iso2, "GB-ENG") == 0)
        snprintf(out, cap, ":england:");
    else if (strcmp(iso2, "GB-SCT") == 0)
        snprintf(out, cap, ":scotland:");
    else if (strcmp(iso2, "GB-WLS") == 0)
        snprintf(out, cap, ":wales:");
    else if (strcmp(iso2, "GB-NIR") == 0)
        snprintf(out, cap, ":flag_gb:");
    else if (strlen(iso2) == 2)
        snprintf(out, cap, ":flag_%c%c:", iso2[0] | 0x20, iso2[1] | 0x20);
    else    /* SWOS 2020's: the pirate flag, as the character */
        snprintf(out, cap, "\xF0\x9F\x8F\xB4\xE2\x80\x8D\xE2\x98\xA0\xEF\xB8\x8F");
}

static void add(const char *text, int32_t injuryTeam)
{
    if (s_frame <= s_confirmed) {           /* (a frame already confirmed: nothing to wait for) */
        if (injuryTeam)
            s_injuries[injuryTeam - 1]++;
        post(ticker_type(), text);
        return;
    }
    if (s_pendingCount == PENDING_MAX)
        return;
    s_pending[s_pendingCount].frame = s_frame;
    s_pending[s_pendingCount].injuryTeam = injuryTeam;
    snprintf(s_pending[s_pendingCount].text, sizeof s_pending[0].text, "%s", text);
    s_pendingCount++;
}

void LiveTicker_Begin(const LiveTickerMatch *match)
{
    char text[400], flag1[48], flag2[48], mode[80];

    s_m = *match;
    s_enabled = s_cfgOn && (s_webhook[0] || (PROJECT_DISCORD_PROXY[0] && !local_server() &&
                                             !getenv("SWOS_UNITED_DRY_RUN")));
    s_collect = s_enabled && s_m.host;
    s_frame = 0;
    s_confirmed = -1;
    s_pendingCount = 0;
    s_injuries[0] = s_injuries[1] = 0;
    s_goal.have = 0;
    s_penaltyTeam = 0;
    memset(s_snap, 0xFF, sizeof s_snap);
    fprintf(stderr, "[ticker] this match %s on Discord%s\n", s_enabled ? "goes" : "does not go",
            !s_enabled ? "" : s_webhook[0] ? " (the test webhook)" : s_collect ? " (this machine posts)" : "");
    if (!s_collect)
        return;

    flag_emoji(s_m.country[0], flag1, sizeof flag1);
    flag_emoji(s_m.country[1], flag2, sizeof flag2);
    if (s_m.mode == LOBBY_MODE_RANKED)
        snprintf(mode, sizeof mode, "- **RANKED MATCH**");
    else if (s_m.mode == LOBBY_MODE_LEAGUE)
        snprintf(mode, sizeof mode, "- **%s**", s_m.tournament[0] ? s_m.tournament : "TOURNAMENT");
    else
        snprintf(mode, sizeof mode, "- **FRIENDLY MATCH**");
    {
        char *c;
        for (c = mode; *c; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 'a' + 'A');
    }
    snprintf(text, sizeof text, ":tv: :radio: %s *Welcome to* ***%s %s (%s)*** *vs.* ***%s %s (%s)*** %s",
             s_m.style >= 0 && s_m.style < GAMEPLAY_STYLES ? k_styleEmoji[s_m.style] : "",
             s_m.name[0], flag1, s_m.team[0], s_m.name[1], flag2, s_m.team[1], mode);
    post(ticker_type(), text);
}

void LiveTicker_FrameBegin(int32_t frame)
{
    char text[320];
    Snap now, *prev;
    int32_t i, n;

    if (!s_collect)
        return;
    /* Played again: what this frame and the ones after it reported is gone until they say it again. */
    for (i = n = 0; i < s_pendingCount; i++)
        if (s_pending[i].frame < frame)
            s_pending[n++] = s_pending[i];
    s_pendingCount = n;
    s_frame = frame;

    now.frame = frame;
    now.gameState = _gameState;
    now.minute = (int16_t)minute();
    now.shootout = (int16_t)_penaltiesState[0] == -1;
    prev = &s_snap[(uint32_t)(frame - 1) & (SNAP_FRAMES - 1)];
    if (frame == 0) {
        uint32_t pitch = _pitchNumberAndType & 0xFF;
        snprintf(text, sizeof text, EMO_BALL " *(0 min.)* %s Kick-off (Pitch Type: %s)", vs(),
                 pitch < 7 ? k_pitchNames[pitch] : "?");
        add(text, 0);
    } else if (prev->frame == frame - 1) {
        if (now.gameState != prev->gameState && now.gameState == ST_FIRST_HALF_ENDED) {
            snprintf(text, sizeof text, ":hourglass: *(45 mins.)* %s *It's halftime...* : **%d - %d**",
                     vs(), goals(1), goals(2));
            add(text, 0);
        }
        {
            /* 105 only: at 90 and 120 the match is over, or goes on to extra time or the shoot-out,
             * which say so ("Match is over!", the shoot-out's line) */
            static const int16_t marks[1] = { 105 };
            for (i = 0; i < 1; i++)
                if (prev->minute < marks[i] && now.minute >= marks[i]) {
                    snprintf(text, sizeof text, ":hourglass: *(%d mins.)* %s **%d - %d**", marks[i], vs(),
                             goals(1), goals(2));
                    add(text, 0);
                }
        }
        if (now.shootout && !prev->shootout) {
            snprintf(text, sizeof text, EMO_PLAY " *(%d mins.)* %s *Penalty shootout started*", now.minute, vs());
            add(text, 0);
        }
    }
    s_snap[(uint32_t)frame & (SNAP_FRAMES - 1)] = now;
}

void LiveTicker_Confirmed(int32_t frame)
{
    int32_t i, n;
    if (!s_collect || frame <= s_confirmed)
        return;
    s_confirmed = frame;
    for (i = n = 0; i < s_pendingCount; i++) {
        if (s_pending[i].frame <= frame) {
            if (s_pending[i].injuryTeam)
                s_injuries[s_pending[i].injuryTeam - 1]++;
            post(ticker_type(), s_pending[i].text);
        } else {
            s_pending[n++] = s_pending[i];
        }
    }
    s_pendingCount = n;
}

void LiveTicker_Report(int32_t kind, int32_t team, const int8_t *name1, const int8_t *name2, int32_t arg)
{
    char text[320], n1[24], n2[24];
    if (!s_collect || team < 1 || team > 2)
        return;
    copy_name(n1, sizeof n1, name1, 15);
    if (kind == LIVE_TICKER_INJURY) {
        snprintf(text, sizeof text, "%s *(%d mins.)* %s **%s (%s)** *got injured*",
                 k_injuryEmoji[arg >= 1 && arg <= 7 ? arg : 0], minute(), vs(), n1, team_name(team));
        add(text, team);
    } else if (kind == LIVE_TICKER_SUB && name2) {
        copy_name(n2, sizeof n2, name2, 15);
        snprintf(text, sizeof text, ":arrows_counterclockwise: *(%d mins.)* %s **%s** *comes in for* **%s** **(%s)**",
                 minute(), vs(), n1, n2, team_name(team));
        add(text, 0);
    }
}

void LiveTicker_DosEvent(uint8_t event, uint16_t arg)
{
    char text[320], name[24];
    int32_t team;

    if (!s_collect)
        return;
    switch (event) {
    case DOS_EV_GOAL:
    case DOS_EV_OWN_GOAL: {             /* (the scorer as GoalScored takes them, handleGoalScored) */
        Sprite *s = _lastPlayerPlayed;
        if (_lastKeeperPlayed && (s == &_goalie1Sprite || s == &_goalie2Sprite))
            s = _lastKeeperPlayed;
        sprite_name(s, s_goal.name, sizeof s_goal.name);
        s_goal.own = event == DOS_EV_OWN_GOAL;
        team = s ? s->teamNumber : 0;
        s_goal.team = s_goal.own ? 3 - team : team;
        s_goal.shootout = arg & 1;
        s_goal.have = 1;
        s_penaltyTeam = 0;
        break;
    }
    case DOS_EV_SCORE:                  /* right after the goal: the new score */
        if (!s_goal.have)
            break;
        s_goal.have = 0;
        if (s_goal.shootout)
            snprintf(text, sizeof text, "%s *(%d mins.)* %s **%s** *%s* **%s** in penalty shootout : **%d - %d**",
                     s_goal.own ? ":man_facepalming:" : ":soccer:", minute(), vs(), s_goal.name,
                     s_goal.own ? "scored an own goal for" : "scored for", team_name(s_goal.team),
                     (int)_team1PenaltyGoals, (int)_team2PenaltyGoals);
        else
            snprintf(text, sizeof text, "%s *(%d mins.)* %s **%s** *%s* **%s** : **%d - %d**",
                     s_goal.own ? ":man_facepalming:" : ":soccer:", minute(), vs(), s_goal.name,
                     s_goal.own ? "scored an own goal for" : "scored for", team_name(s_goal.team),
                     goals(1), goals(2));
        add(text, 0);
        break;
    case DOS_EV_YELLOW_CARD:
    case DOS_EV_RED_CARD:
        sprite_name(_bookedPlayer, name, sizeof name);
        snprintf(text, sizeof text, "%s *(%d mins.)* %s **%s (%s)** *%s*",
                 event == DOS_EV_RED_CARD ? EMO_RED : EMO_YELLOW, minute(), vs(), name,
                 team_name(_bookedPlayer ? _bookedPlayer->teamNumber : 0),
                 event == DOS_EV_RED_CARD ? "got sent off" : "got booked");
        add(text, 0);
        break;
    case DOS_EV_WOODWORK:               /* (SWOS 2020: the team that played it last) */
        team = _lastTeamPlayed ? _lastTeamPlayed->teamNumber : 0;
        snprintf(text, sizeof text, "%s *(%d mins.)* %s **%s** *hit the %s!!!* : **%d - %d**",
                 arg ? ":grey_exclamation:" : ":white_square_button:", minute(), vs(), team_name(team),
                 arg ? "post" : "bar", goals(1), goals(2));
        add(text, 0);
        break;
    case DOS_EV_CORNER:                 /* for the other team (game_timers.c) */
        team = _lastTeamPlayed ? 3 - _lastTeamPlayed->teamNumber : 0;
        snprintf(text, sizeof text, EMO_PLAY " *(%d mins.)* %s *Corner kick for* **%s**... it is still **%d - %d**",
                 minute(), vs(), team_name(team), goals(1), goals(2));
        add(text, 0);
        break;
    case DOS_EV_PENALTY:                /* the team fouled: camera direction 0 is the top team's */
        team = (_cameraDirection == 0 ? &_topTeamData : &_bottomTeamData)->teamNumber;
        s_penaltyTeam = team;
        snprintf(text, sizeof text, EMO_PLAY " *(%d mins.)* %s *Penalty for* **%s** : **%d - %d**",
                 minute(), vs(), team_name(team), goals(1), goals(2));
        add(text, 0);
        break;
    case DOS_EV_KEEPER_SAVED:
    case DOS_EV_NEAR_MISS:              /* a penalty in the match missed (the shoot-out's are its score) */
        if (!_penalty || (arg & 1) || !s_penaltyTeam)
            break;
        snprintf(text, sizeof text, ":x: *(%d mins.)* %s **%s** *MISSED A PENALTY!!!* : **%d - %d**",
                 minute(), vs(), team_name(s_penaltyTeam), goals(1), goals(2));
        s_penaltyTeam = 0;
        add(text, 0);
        break;
    default:
        break;
    }
}

/* ---- the end ---- */

/* SWOS 2020's getScoreStr(true): "**2 - 1**", with the aggregate and the shoot-out. */
static void score_str(char *out, uint32_t cap)
{
    char agg[48] = "", pens[48] = "";
    if (s_m.legs == 2 && s_m.leg == 2)
        snprintf(agg, sizeof agg, "**%d - %d** ON AGG, ", s_m.firstLeg1 + goals(1), s_m.firstLeg2 + goals(2));
    if (_team1PenaltyGoals || _team2PenaltyGoals)
        snprintf(pens, sizeof pens, ", **%d - %d** ON PENS%s", (int)_team1PenaltyGoals, (int)_team2PenaltyGoals,
                 minute() > 90 ? " AET" : "");
    snprintf(out, cap, "%s**%d - %d**%s", agg, goals(1), goals(2), pens);
}

/* ... and getWonStr(true). */
static void won_str(char *out, uint32_t cap)
{
    int32_t a = goals(1), b = goals(2);
    if (s_m.legs == 2 && s_m.leg == 1) {
        snprintf(out, cap, "*1ST LEG:*");
        return;
    }
    if (s_m.legs == 2) {
        a += s_m.firstLeg1;
        b += s_m.firstLeg2;
    }
    if (a == b) {
        a = _team1PenaltyGoals;
        b = _team2PenaltyGoals;
    }
    if (a == b)
        snprintf(out, cap, "*GAME TIED*");
    else
        snprintf(out, cap, "**%s** *WON*", s_m.name[a > b ? 0 : 1]);
}

static void score_board(void)
{
    static const char *const kModes[3] = { "FRIENDLY", "TOURNAMENT", "RANKED" };
    const TeamStatisticsData *t1 = team_info(1)->teamStatsPtr, *t2 = team_info(2)->teamStatsPtr;
    char text[1800], score[96], flag1[48], flag2[48], mode[96], leg[16] = "";
    int32_t o = 0, poss = 0, total;

    if (!t1 || !t2)
        return;
    flag_emoji(s_m.country[0], flag1, sizeof flag1);
    flag_emoji(s_m.country[1], flag2, sizeof flag2);
    score_str(score, sizeof score);
    if (s_m.mode == LOBBY_MODE_LEAGUE && s_m.tournament[0])
        snprintf(mode, sizeof mode, "TOURNAMENT (%s)", s_m.tournament);
    else
        snprintf(mode, sizeof mode, "%s", kModes[s_m.mode >= 0 && s_m.mode <= 2 ? s_m.mode : 0]);
    {
        char *c;
        for (c = mode; *c; c++)
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 'a' + 'A');
    }
    if (s_m.legs == 2)
        snprintf(leg, sizeof leg, s_m.leg == 1 ? " (1ST LEG)" : " (2ND LEG)");
    total = (uint16_t)t1->ballPossession + (uint16_t)t2->ballPossession;
    if (total)
        poss = (int32_t)((uint32_t)(uint16_t)t1->ballPossession * 100u / (uint32_t)total);

#define LINE(...) o += snprintf(text + o, o < (int32_t)sizeof text ? sizeof text - (uint32_t)o : 0, __VA_ARGS__)
    LINE(EMO_PLAY " **%s** %s *(%s)* vs **%s** %s *(%s)* %s " EMO_TICK "\n",
         s_m.name[0], flag1, s_m.team[0], s_m.name[1], flag2, s_m.team[1], score);
    LINE("```Location     :   %s - %s\n", s_m.location[0][0] ? s_m.location[0] : "?",
         s_m.location[1][0] ? s_m.location[1] : "?");
    LINE("Game Mode    :   %s\n", mode);
    LINE("Game Style   :   %s\n", Gameplay_StyleName(s_m.style));
    LINE("Game DB      :   %s\n", s_m.teamDb);
    LINE("Pitch Type   :   %s\n", (_pitchNumberAndType & 0xFF) < 7 ? k_pitchNames[_pitchNumberAndType & 0xFF] : "?");
    LINE("Tactics      :   %s - %s\n", s_m.tactic[0], s_m.tactic[1]);
    if (s_m.legs == 2 && s_m.leg == 2)
        LINE("Score        :  %2d  -  %d (AGG.)\n", s_m.firstLeg1 + goals(1), s_m.firstLeg2 + goals(2));
    LINE("Score        :  %2d  -  %d%s\n", goals(1), goals(2), leg);
    if (_team1PenaltyGoals || _team2PenaltyGoals)
        LINE("Score        :  %2d  -  %d (PENS.)\n", (int)_team1PenaltyGoals, (int)_team2PenaltyGoals);
    LINE("Possession   : %2d%%  -  %d%%\n", total ? poss : 0, total ? 100 - poss : 0);
    LINE("Attempts     :  %2d  -  %d\n", t1->goalAttempts, t2->goalAttempts);
    LINE("On Target    :  %2d  -  %d\n", t1->onTarget, t2->onTarget);
    LINE("Corners      :  %2d  -  %d\n", t1->cornersWon, t2->cornersWon);
    LINE("Fouls        :  %2d  -  %d\n", t1->foulsConceded, t2->foulsConceded);
    LINE("Yellow Cards :  %2d  -  %d\n", t1->bookings, t2->bookings);
    LINE("Red Cards    :  %2d  -  %d\n", t1->sendingsOff, t2->sendingsOff);
    LINE("Injuries     :  %2d  -  %d\n```", s_injuries[0], s_injuries[1]);
#undef LINE
    post(board_type(), text);
}

void LiveTicker_End(int32_t finished, const char *why)
{
    char text[400], won[64], score[96];
    const char *me, *other;

    if (!s_enabled)
        return;
    s_enabled = 0;
    me = s_m.name[s_m.localPlayer == 2 ? 1 : 0];
    other = s_m.name[s_m.localPlayer == 2 ? 0 : 1];
    if (finished) {
        if (!s_collect)
            return;
        LiveTicker_Confirmed(0x7FFFFFFF);       /* every frame was played */
        won_str(won, sizeof won);
        score_str(score, sizeof score);
        snprintf(text, sizeof text, ":alarm_clock: *(%d mins.)* %s *Match is over!* %s. %s", minute(), vs(), won, score);
        post(ticker_type(), text);
        score_board();
        s_collect = 0;
        return;
    }
    s_collect = 0;
    /* Cut short. The player who left says so -- the other's game hears "THE OTHER PLAYER LEFT"
     * and says nothing, so it is said once; a game that went quiet (killed, crashed, the network)
     * is said by the one still there. */
    if (!why)
        why = "";
    if (strcmp(why, "YOU LEFT THE MATCH") == 0 || strcmp(why, "YOU CLOSED THE GAME") == 0) {
        snprintf(text, sizeof text, ":bangbang: *(%d mins.)* %s **%s** quit the match with *%s* : **%d - %d**",
                 minute(), vs(), me, why[4] == 'L' ? "ESC" : "the window closed", goals(1), goals(2));
        post(ticker_type(), text);
        post("quitter_report", text);
    } else if (strcmp(why, "THE CONNECTION WENT QUIET") == 0 || strcmp(why, "A NETWORK ERROR ENDED THE MATCH") == 0) {
        snprintf(text, sizeof text, ":rotating_light: *(%d mins.)* %s **%s** *lost the connection to* **%s** : **%d - %d**",
                 minute(), vs(), me, other, goals(1), goals(2));
        post(ticker_type(), text);
        post("quitter_report", text);
    } else if (strcmp(why, "THE TWO GAMES WENT OUT OF STEP") == 0 && s_m.host) {
        snprintf(text, sizeof text, ":rotating_light: *(%d mins.)* %s *The match ended: the two games went out of step* : **%d - %d**",
                 minute(), vs(), goals(1), goals(2));
        post(ticker_type(), text);
    }
}

#endif /* SWOS_SDL_BUILD */
