/* live_cast_fork.inc -- SWOS United's own live matches in LIVE CAST: SWOS 2020's (src/live_cast.c
 * includes this at its end; OpenSWOS has no such file, so a merge of OpenSWOS never meets it).
 *
 * SWOS 2020 streams its online matches to its livestream server on swos.eu, the way its web
 * viewer watches them: SWOS-API's united_live2020.c lists the matches live now and carries a
 * match's stream (include/united_live2020.h). This file turns that stream into what LIVE CAST
 * plays -- the .SRP's META and frames (docs/REPLAY_FORMAT_V2.md) -- in this file's buffers, so
 * the viewer, the live edge and the end of the match are LIVE CAST's own:
 *
 *   the match's JSON -> META: the teams' names and the kits they wear (as the primary and the
 *       secondary), the pitch, the game style -- its rate: 70 the PC's, 60 Amiga 60's (3, 4),
 *       50 the Amiga's and Moon Soccer's (dos_replay_load.c replay_fps_from_gamestyle) -- and the
 *       picture's size;
 *       the line-ups from the 0x03 messages (a slot each: SWOS 2020's PlayerGame, our PlayerInfo,
 *       61 bytes both), as they stand when the first frame comes;
 *   a 0x00 frame -> a frame: SWOS 2020's camera is the left / top of ITS picture (480 x 272 as a
 *       rule) and a sprite's x / y its place on that picture, so a sprite's world position is the
 *       two added; our camera is the simulation's, the picture's scaled back (camera.c
 *       Render_CameraX16's inverse for that picture's width); the PC's sprite numbers less 114
 *       (dos_replay_load.c, the same 2020 frames in a .RPL); at most the 78 a frame holds.
 *
 * No sound comes with the stream, so none is heard; a match is watched from where it is when it
 * is joined. Its ids in the list: "S2" in the top 16 bits, the row of the last list below. */

#if defined(SWOS_HTTPS)
#include "united_live2020.h"
#include "endian_helpers.h"
#include "mjson.h"
#include "project_identity.h"
#include <time.h>

enum {
    kL2Max = 32,
    kL2MetaBytes = 2 + 2 + 2 + 2 + 4 + 32 + 4 + 2 + 2 + 2 + 40 + 40 + 2 * 1018,   /* replay_v2.c META_BYTES */
    kL2TeamBytes = 1018, kL2PlayersOff = 42, kL2PlayerBytes = 61,
    kL2SimPan = 0x2A0 - 336,            /* the simulation camera's range (SCREEN_WIDTH 336) */
    kL2MaxSprites = 78, kL2DosSprites = 114,
};
#define L2_ID(k)    ((0x5332ull << 48) | (uint64_t)(k))
#define L2_OWNS(id) (((id) >> 48) == 0x5332ull)

static char                 s_l2Ids[kL2Max][40];        /* the last list's streams, by row */
static int32_t              s_l2Count;
static UnitedLive2020Watch *s_l2Watch;
static SDL_Thread          *s_l2Thread;
static char                 s_l2Json[4096];             /* the match, as the server last said it */
static uint8_t              s_l2Players[2][16][kL2PlayerBytes];
static int32_t              s_l2Width = 480, s_l2Height = 272, s_l2Style = 1;

static int32_t l2_list(ModLiveMatch *out, int32_t cap)
{
    UnitedLive2020 m[kL2Max];
    int32_t n = United_Live2020List(m, kL2Max);
    int32_t k;
    if (n < 0)
        return -1;
    {   /* (only the matches being played: one still in its lobby streams no frames yet) */
        int32_t live = 0;
        for (k = 0; k < n; k++)
            if (m[k].state == 1)
                m[live++] = m[k];
        n = live;
    }
    s_l2Count = n;
    for (k = 0; k < n; k++)
        snprintf(s_l2Ids[k], sizeof s_l2Ids[k], "%s", m[k].streamId);
    if (!out)
        return n;
    for (k = 0; k < n && k < cap; k++) {
        memset(&out[k], 0, sizeof out[k]);
        out[k].id = L2_ID(k);
        memcpy(out[k].players, m[k].players, sizeof out[k].players);
        memcpy(out[k].teams, m[k].teams, sizeof out[k].teams);
        for (int i = 0; i < 2; i++) {
            out[k].players[i][23] = 0;
            out[k].teams[i][23] = 0;
        }
        out[k].frames = (uint32_t)m[k].minute * 60u * 50u / 90u;      /* (roughly: how far it is) */
        out[k].minute = m[k].minute;
        out[k].goals[0] = m[k].goals[0];
        out[k].goals[1] = m[k].goals[1];
    }
    return k;
}

static int32_t l2_owns(uint64_t id)
{
    return L2_OWNS(id) && (int32_t)(id & 0xFFFF) < s_l2Count;
}

static int32_t l2_num(const char *path, int32_t fallback)
{
    double v;
    return mjson_get_number(s_l2Json, (int)strlen(s_l2Json), path, &v) ? (int32_t)v : fallback;
}

/* A team as the .HIL header holds it (replay_v2.c put_team): the kit it wears as both of its own,
 * big-endian; its name; its line-up. */
static void l2_team(uint8_t *out, int32_t t)
{
    static const char *const kit[5] = { "shirtType", "shirtCol", "stripesCol", "shortsCol", "socksCol" };
    static const char *const sec[5] = { "shirtType", "secShirtCol", "secStripesCol", "secShortsCol", "socksCol" };
    char path[64], name[24] = "";
    memset(out, 0, kL2TeamBytes);
    for (int i = 0; i < 5; i++) {
        snprintf(path, sizeof path, "$.team%dKit.%s", (int)t + 1, kit[i]);
        const int32_t v = l2_num(path, 0);
        BE_WRITE_U16(out + 2 * i, (uint16_t)v);
        snprintf(path, sizeof path, "$.team%dKit.%s", (int)t + 1, sec[i]);
        BE_WRITE_U16(out + 10 + 2 * i, (uint16_t)l2_num(path, v));
    }
    snprintf(path, sizeof path, "$.team%dName", (int)t + 1);
    mjson_get_string(s_l2Json, (int)strlen(s_l2Json), path, name, sizeof name);
    for (char *c = name; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    memcpy(out + 22, name, strlen(name) < 16 ? strlen(name) : 16);
    memcpy(out + kL2PlayersOff, s_l2Players[t], sizeof s_l2Players[t]);
}

/* META (replay_v2.c build_meta), from the match as it stands. */
static uint8_t *l2_meta(uint16_t goals1, uint16_t goals2)
{
    uint8_t *m = (uint8_t *)calloc(1, kL2MetaBytes), *q = m;
    char type[40] = "";
    if (!m)
        return NULL;
    BE_WRITE_U16(q, 1);                                         q += 2;
    BE_WRITE_U16(q, (uint16_t)s_l2Width);                       q += 2;
    /* 1 the PC's gameplay; 8 the DOS view (a picture 200 lines tall); 16 the view is recorded;
     * 64 AMIGA60, 128 MOON (replay_v2.c build_meta) */
    BE_WRITE_U16(q, (uint16_t)((s_l2Style == 0 ? 1 : 0) | (s_l2Height <= 200 ? 8 : 0) | 16 |
                               (s_l2Style == 3 || s_l2Style == 4 ? 64 : 0) | (s_l2Style == 2 ? 128 : 0)));
    q += 2;
    BE_WRITE_U16(q, (uint16_t)s_l2Height);                      q += 2;
    BE_WRITE_U32(q, (uint32_t)time(NULL));                      q += 4;
    snprintf((char *)q, 32, "SWOS 2020 LIVE");                  q += 32;
    BE_WRITE_U16(q, goals1);                                    q += 2;
    BE_WRITE_U16(q, goals2);                                    q += 2;
    /* (the JSON's pitch 1-6, the .SRP's 0-5: swos.eu's srparchive.go reads it back +1) */
    BE_WRITE_U16(q, (uint16_t)(((l2_num("$.pitchNumber", 1) - 1) & 0xFF) << 8 | (l2_num("$.pitchType", 0) & 0xFF)));
    q += 2;
    BE_WRITE_U16(q, 5);                                         q += 2;
    q += 2;
    mjson_get_string(s_l2Json, (int)strlen(s_l2Json), "$.matchType", type, sizeof type);
    snprintf((char *)q, 40, "SWOS 2020");                       q += 40;
    for (char *c = type; *c; c++)
        if (*c >= 'a' && *c <= 'z')
            *c = (char)(*c - 32);
    snprintf((char *)q, 40, "%s", type);                        q += 40;
    l2_team(q, 0);                                              q += kL2TeamBytes;
    l2_team(q, 1);
    return m;
}

static int16_t l2_le16(const uint8_t *p) { return (int16_t)(p[0] | (p[1] << 8)); }

/* A 0x00 frame (StreamProtocol.cpp encodeFrame) into the frames LIVE CAST holds. Under s_lock. */
static int l2_frame(const uint8_t *p, uint32_t len)
{
    if (len < 16)
        return -1;
    const uint8_t minute = p[1];
    const int16_t camX = l2_le16(p + 6), camY = l2_le16(p + 8);
    const uint16_t g1 = (uint16_t)l2_le16(p + 10), g2 = (uint16_t)l2_le16(p + 12);
    const uint16_t anim = (uint16_t)l2_le16(p + 14);
    uint32_t count = p[16], k, n, at = 17;
    if (len < 17 + count * 6)
        return -1;
    if (!s_meta) {                      /* (the first frame: the match as it stands is META) */
        s_l2Width = l2_num("$.resolution[0]", 480);
        s_l2Height = l2_num("$.resolution[1]", 272);
        s_l2Style = l2_num("$.gameStyle", 1);
        s_tick = (uint16_t)(s_l2Style == 0 ? 70 : s_l2Style == 3 || s_l2Style == 4 ? 60 : 50);
        s_quirks = 0;
        s_meta = l2_meta(g1, g2);
        if (!s_meta)
            return -1;
        s_metaLen = kL2MetaBytes;
    }
    if (count > kL2MaxSprites)
        count = kL2MaxSprites;
    n = kFrameHead + kSpriteBytes * count;
    if (grow((void **)&s_frms, &s_frmsCap, s_frmsLen + n, 1) ||
        grow((void **)&s_offsets, &s_offsetsCap, s_frames + 1, sizeof *s_offsets))
        return -1;
    {
        const int32_t pan = 0x2A0 - s_l2Width;
        int32_t simX = pan > 0 ? (int32_t)camX * kL2SimPan / pan : 0;
        uint8_t *o = s_frms + s_frmsLen;
        if (simX < 0) simX = 0;
        else if (simX > kL2SimPan) simX = kL2SimPan;
        BE_WRITE_U16(o, (uint16_t)count);
        BE_WRITE_U32(o + 2, (uint32_t)simX << 16);
        BE_WRITE_U32(o + 6, (uint32_t)(uint16_t)(camY < 0 ? 0 : camY) << 16);
        BE_WRITE_U16(o + 10, g1);
        BE_WRITE_U16(o + 12, g2);
        BE_WRITE_U16(o + 14, anim);
        BE_WRITE_U16(o + 16, minute);
        BE_WRITE_U16(o + 18, 0);
        o += kFrameHead;
        for (k = 0; k < count; k++, at += 6, o += kSpriteBytes) {
            uint16_t idx = (uint16_t)l2_le16(p + at);
            if (idx >= kL2DosSprites)
                idx = (uint16_t)(idx - kL2DosSprites);
            BE_WRITE_U16(o, idx);
            BE_WRITE_U16(o + 2, (uint16_t)(camX + l2_le16(p + at + 2)));
            BE_WRITE_U16(o + 4, (uint16_t)(camY + l2_le16(p + at + 4)));
        }
    }
    s_offsets[s_frames++] = s_frmsLen;
    s_frmsLen += n;
    s_score[0] = g1;
    s_score[1] = g2;
    s_haveScore = 1;
    return 0;
}

static int SDLCALL l2_reader(void *unused)
{
    int32_t text;
    const uint8_t *data;
    uint32_t len;
    (void)unused;
    while (United_Live2020Read(s_l2Watch, &text, &data, &len) == 0) {
        SDL_LockMutex(s_lock);
        if (text) {                                         /* the match (the first message) */
            snprintf(s_l2Json, sizeof s_l2Json, "%.*s", (int)len, (const char *)data);
        } else if (len >= 1 && data[0] == 0x07 && len > 1) {    /* the match again, corrected */
            snprintf(s_l2Json, sizeof s_l2Json, "%.*s", (int)(len - 1), (const char *)data + 1);
        } else if (len >= 3 && data[0] == 0x03 && data[1] < 2) {    /* line-up slots */
            const uint8_t t = data[1];
            uint32_t at = 3;
            for (uint32_t k = 0; k < data[2] && at + 1 + kL2PlayerBytes <= len; k++, at += 1 + kL2PlayerBytes)
                if (data[at] < 16)
                    memcpy(s_l2Players[t][data[at]], data + at + 1, kL2PlayerBytes);
        } else if (len >= 1 && data[0] == 0x00) {
            if (l2_frame(data, len) != 0) {
                SDL_UnlockMutex(s_lock);
                fprintf(stderr, "[live2020] a malformed frame: stopped\n");
                break;
            }
        }
        SDL_UnlockMutex(s_lock);
    }
    s_done = 1;
    return 0;
}

static int32_t l2_start(uint64_t id)
{
    const int32_t k = (int32_t)(id & 0xFFFF);
    s_l2Json[0] = 0;
    memset(s_l2Players, 0, sizeof s_l2Players);
    s_l2Watch = United_Live2020Open(s_l2Ids[k]);
    if (!s_l2Watch)
        return 1;
    snprintf(s_host, sizeof s_host, "%s", PROJECT_LIVE2020_HOST);    /* (LIVE CAST's lines name it) */
    s_port = PROJECT_LIVE2020_WATCH_PORT;
    s_l2Thread = SDL_CreateThread(l2_reader, "live-2020", NULL);
    return s_l2Thread ? 0 : 1;
}

static void l2_stop(void)
{
    United_Live2020Stop(s_l2Watch);
    if (s_l2Thread) {
        SDL_WaitThread(s_l2Thread, NULL);
        s_l2Thread = NULL;
    }
    United_Live2020Close(s_l2Watch);
    s_l2Watch = NULL;
}

static const LiveCastFork *live_cast_fork(void)
{
    /* 1 s behind: the stream comes from the live edge, a frame every 20 ms (no backlog to buffer) */
    static const LiveCastFork fork = { l2_list, l2_owns, l2_start, l2_stop, 1 };
    return &fork;
}

#else   /* (no HTTPS: no SWOS 2020 matches) */
static const LiveCastFork *live_cast_fork(void) { return NULL; }
#endif
