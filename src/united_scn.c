/*
 * united_scn.c -- SWOS 2020's custom challenges (.SCN) into OpenSWOS's challenges (include/united_scn.h).
 *
 * A .SCN is a 7z archive (LZMA2) of challenge.ini -- one [scenario], the fields of SWOS 2020's
 * challenges.ini -- and the two teams as .TEAM files (684-byte team records, the PC's: their one
 * 16-bit field little-endian). It becomes:
 *   - <save>/mods/challenges/scn-<hash>.EDT    the two teams (an .EDT: big-endian count and field)
 *   - a challenge in <save>/mods/challenges/shared.json (openswos-challenges, version 2), which the
 *     challenges mod reads after the player's own (OpenSWOS cea125ac) -- by the same rules as
 *     OpenSWOS's scripts/convert_swos2020_challenges.py, which converts SWOS 2020's own pack.
 * The 7z decoding is the LZMA SDK's (third_party/lzma, public domain).
 */
#include "mod_api.h"

#if defined(SWOS_SDL_BUILD)

#include "united_scn.h"

#include "7z.h"
#include "7zAlloc.h"
#include "7zCrc.h"
#include "mjson.h"

#include <SDL.h>                   /* SDL_strcasecmp */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { kTeamRecord = 684 };
enum { kFetchedKept = 500 };    /* downloaded challenges kept (shared.json), the newest */

/* ---- the 7z archive, from memory ---- */

typedef struct { ISeekInStream vt; const Byte *data; size_t size, pos; } MemStream;

static SRes mem_read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
    MemStream *p = Z7_CONTAINER_FROM_VTBL(pp, MemStream, vt);
    size_t n = p->size - p->pos;
    if (*size < n)
        n = *size;
    memcpy(buf, p->data + p->pos, n);
    p->pos += n;
    *size = n;
    return SZ_OK;
}

static SRes mem_seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
    MemStream *p = Z7_CONTAINER_FROM_VTBL(pp, MemStream, vt);
    const Int64 base = origin == SZ_SEEK_SET ? 0 : origin == SZ_SEEK_CUR ? (Int64)p->pos : (Int64)p->size;
    const Int64 np = base + *pos;
    if (np < 0 || (UInt64)np > p->size)
        return SZ_ERROR_PARAM;
    p->pos = (size_t)np;
    *pos = np;
    return SZ_OK;
}

typedef struct {
    char *ini;                          /* challenge.ini (malloc'd, NUL-terminated) */
    struct { char file[64]; unsigned char rec[kTeamRecord]; } teams[4];
    int32_t numTeams;
} Scn;

static int32_t unpack(const unsigned char *data, uint32_t size, Scn *out)
{
    static int crcReady;
    ISzAlloc alloc = { SzAlloc, SzFree };
    MemStream ms;
    CLookToRead2 look;
    CSzArEx db;
    UInt32 blockIndex = 0xFFFFFFFF;
    Byte *outBuf = NULL;
    size_t outSize = 0;
    Byte *lookBuf = (Byte *)malloc(1 << 14);
    int32_t ok = 1;
    memset(out, 0, sizeof *out);
    if (!lookBuf)
        return 0;
    if (!crcReady) {
        CrcGenerateTable();
        crcReady = 1;
    }
    ms.vt.Read = mem_read;
    ms.vt.Seek = mem_seek;
    ms.data = data;
    ms.size = size;
    ms.pos = 0;
    LookToRead2_CreateVTable(&look, False);
    look.buf = lookBuf;
    look.bufSize = 1 << 14;
    look.realStream = &ms.vt;
    LookToRead2_INIT(&look)
    SzArEx_Init(&db);
    if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) {
        SzArEx_Free(&db, &alloc);
        free(lookBuf);
        return 0;
    }
    for (UInt32 i = 0; i < db.NumFiles && ok; i++) {
        size_t offset = 0, got = 0;
        UInt16 wname[128];
        char name[128];
        if (SzArEx_IsDir(&db, i) || SzArEx_GetFileNameUtf16(&db, i, NULL) > 128)
            continue;
        const size_t len = SzArEx_GetFileNameUtf16(&db, i, wname);
        for (size_t k = 0; k < len; k++)
            name[k] = (char)(wname[k] < 128 ? wname[k] : '_');
        name[len ? len - 1 : 0] = 0;
        if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outSize, &offset, &got, &alloc, &alloc) != SZ_OK) {
            ok = 0;
            break;
        }
        const size_t n = strlen(name);
        if (n > 4 && !strcmp(name + n - 4, ".ini")) {
            free(out->ini);
            out->ini = (char *)malloc(got + 1);
            if (out->ini) {
                memcpy(out->ini, outBuf + offset, got);
                out->ini[got] = 0;
            }
        } else if (n > 5 && !strcmp(name + n - 5, ".TEAM") && got == kTeamRecord && out->numTeams < 4) {
            snprintf(out->teams[out->numTeams].file, sizeof out->teams[0].file, "%s", name);
            memcpy(out->teams[out->numTeams].rec, outBuf + offset, kTeamRecord);
            out->numTeams++;
        }
    }
    ISzAlloc_Free(&alloc, outBuf);
    SzArEx_Free(&db, &alloc);
    free(lookBuf);
    return ok && out->ini;
}

int32_t United7z_FirstFile(const unsigned char *data, uint32_t size, const char *ext, unsigned char **out,
                           uint32_t *outSize)
{
    static int crcReady;
    ISzAlloc alloc = { SzAlloc, SzFree };
    MemStream ms;
    CLookToRead2 look;
    CSzArEx db;
    UInt32 blockIndex = 0xFFFFFFFF;
    Byte *outBuf = NULL;
    size_t outBufSize = 0;
    Byte *lookBuf = (Byte *)malloc(1 << 14);
    const size_t extLen = strlen(ext);
    int32_t found = 0;
    *out = NULL;
    *outSize = 0;
    if (!lookBuf)
        return 0;
    if (!crcReady) {
        CrcGenerateTable();
        crcReady = 1;
    }
    ms.vt.Read = mem_read;
    ms.vt.Seek = mem_seek;
    ms.data = data;
    ms.size = size;
    ms.pos = 0;
    LookToRead2_CreateVTable(&look, False);
    look.buf = lookBuf;
    look.bufSize = 1 << 14;
    look.realStream = &ms.vt;
    LookToRead2_INIT(&look)
    SzArEx_Init(&db);
    if (SzArEx_Open(&db, &look.vt, &alloc, &alloc) != SZ_OK) {
        SzArEx_Free(&db, &alloc);
        free(lookBuf);
        return 0;
    }
    for (UInt32 i = 0; i < db.NumFiles && !found; i++) {
        size_t offset = 0, got = 0;
        UInt16 wname[256];
        char name[256];
        if (SzArEx_IsDir(&db, i) || SzArEx_GetFileNameUtf16(&db, i, NULL) > 256)
            continue;
        const size_t len = SzArEx_GetFileNameUtf16(&db, i, wname);
        for (size_t k = 0; k < len; k++)
            name[k] = (char)(wname[k] < 128 ? wname[k] : '_');
        name[len ? len - 1 : 0] = 0;
        const size_t n = strlen(name);
        if (n < extLen || SDL_strcasecmp(name + n - extLen, ext) != 0)
            continue;
        if (SzArEx_Extract(&db, &look.vt, i, &blockIndex, &outBuf, &outBufSize, &offset, &got, &alloc, &alloc) != SZ_OK)
            break;
        *out = (unsigned char *)malloc(got ? got : 1);
        if (*out) {
            memcpy(*out, outBuf + offset, got);
            *outSize = (uint32_t)got;
            found = 1;
        }
    }
    ISzAlloc_Free(&alloc, outBuf);
    SzArEx_Free(&db, &alloc);
    free(lookBuf);
    return found;
}

/* ---- challenge.ini ---- */

/* the value of KEY = value in the ini (case as written; trimmed), into out */
static const char *ini_get(const char *ini, const char *key, char *out, uint32_t cap)
{
    const size_t k = strlen(key);
    out[0] = 0;
    for (const char *line = ini; line && *line;) {
        const char *next = strchr(line, '\n');
        while (*line == ' ' || *line == '\t')
            line++;
        if (!strncmp(line, key, k)) {
            const char *p = line + k;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '=') {
                p++;
                while (*p == ' ' || *p == '\t')
                    p++;
                const char *end = next ? next : p + strlen(p);
                while (end > p && (end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
                    end--;
                snprintf(out, cap, "%.*s", (int)(end - p), p);
                for (char *c = out; *c; c++)
                    if (*c >= 'a' && *c <= 'z')
                        *c = (char)(*c - 32);
                return out;
            }
        }
        line = next ? next + 1 : NULL;
    }
    return out;
}

static int32_t ini_num(const char *ini, const char *key, int32_t fallback)
{
    char v[32];
    ini_get(ini, key, v, sizeof v);
    return v[0] ? atoi(v) : fallback;
}

static int32_t ini_yes(const char *ini, const char *key)
{
    char v[16];
    return !strcmp(ini_get(ini, key, v, sizeof v), "YES");
}

static int32_t side_team(const char *v) { return !strcmp(v, "TEAM1") ? 0 : !strcmp(v, "TEAM2") ? 1 : -1; }

/* the numbers in "9, 10" */
static int32_t numbers(const char *v, int32_t *out, int32_t max)
{
    int32_t n = 0;
    for (const char *p = v; *p && n < max;) {
        while (*p && (*p < '0' || *p > '9'))
            p++;
        if (!*p)
            break;
        out[n++] = atoi(p);
        while (*p >= '0' && *p <= '9')
            p++;
    }
    return n;
}

/* ---- the challenge, as JSON ---- */

typedef struct { char *p; uint32_t left; } Out;
static void put(Out *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Out *o, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(o->p, o->left, fmt, ap);
    va_end(ap);
    if (n > 0 && (uint32_t)n < o->left) {
        o->p += n;
        o->left -= (uint32_t)n;
    }
}

static const char *esc(const char *in, char *out, uint32_t cap) { return ModApi_JsonEscape(in, out, cap); }

static uint32_t fnv32(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

/* A team record's name (trailing spaces off) */
static void record_name(const unsigned char *rec, char *out, uint32_t cap)
{
    snprintf(out, cap, "%.17s", (const char *)rec + 5);
    for (size_t n = strlen(out); n && out[n - 1] == ' ';)
        out[--n] = 0;
}

static int32_t same_team(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'a' && x <= 'z') x = (char)(x - 32);
        if (y >= 'a' && y <= 'z') y = (char)(y - 32);
        if (x != y)
            return 0;
    }
    return !*a && !*b;
}

/* The challenge's JSON object into o; its id into id. 1 done. */
static int32_t challenge_json(const Scn *scn, const char *edt, const char teamNames[2][24], char *id, uint32_t idCap,
                              Out *o)
{
    const char *ini = scn->ini;
    char v[256], e1[300], e2[300];
    char name[96];
    ini_get(ini, "SCENARIO NAME", name, sizeof name);
    if (!name[0])
        return 0;
    snprintf(id, idCap, "scn-%08x", (unsigned)fnv32(name));
    const int32_t cpu = side_team(ini_get(ini, "CPU TEAM", v, sizeof v));
    const int32_t yours = cpu == 1 ? 0 : 1;
    char up[16];
    ini_get(ini, "TEAM PLAYING UP", up, sizeof up);
    const int32_t playingUp = !strcmp(up, "UP") ? 0 : !strcmp(up, "DOWN") ? 1 : -1;
    const int32_t kickOff = side_team(ini_get(ini, "TEAM STARTING", v, sizeof v));
    char shortName[32];
    snprintf(shortName, sizeof shortName, "%.31s", name);

    put(o, "  { \"id\": \"%s\", \"name\": \"%s\", \"record_name\": \"%s\", \"category\": \"CUSTOM\",\n", id,
        esc(shortName, e1, sizeof e1), esc(name, e2, sizeof e2));
    put(o, "    \"title\": \"%s\", ", esc(ini_get(ini, "SCENARIO NICKNAME", v, sizeof v), e1, sizeof e1));
    put(o, "\"difficulty\": %d, \"win_message\": \"%s\",\n", (int)ini_num(ini, "DIFFICULTY", 1),
        esc(ini_get(ini, "WIN MESSAGE", v, sizeof v), e1, sizeof e1));
    {
        char by[64] = "";
        const char *b = strstr(name, " BY ");
        if (b)
            snprintf(by, sizeof by, "SHARED BY %s", b + 4);
        put(o, "    \"about\": \"%s\", \"description\": [", esc(by, e1, sizeof e1));
        for (int32_t k = 1; k <= 5; k++) {
            char key[16];
            snprintf(key, sizeof key, "DESCRIPTION%d", (int)k);
            put(o, "%s\"%s\"", k > 1 ? ", " : "", esc(ini_get(ini, key, v, sizeof v), e1, sizeof e1));
        }
        put(o, "],\n");
    }
    put(o, "    \"teams\": [ { \"file\": \"%s\", \"name\": \"%s\" }, ", edt, esc(teamNames[0], e1, sizeof e1));
    put(o, "{ \"file\": \"%s\", \"name\": \"%s\" } ],\n", edt, esc(teamNames[1], e2, sizeof e2));
    put(o, "    \"yours\": %d, \"score\": [%d, %d], \"minute\": %d,", (int)yours, (int)ini_num(ini, "TEAM1 SCORE", 0),
        (int)ini_num(ini, "TEAM2 SCORE", 0), (int)ini_num(ini, "INITIAL TIME", 0));
    put(o, " \"extra_time\": %s, \"penalties\": %s,", ini_yes(ini, "EXTRA TIME") ? "true" : "false",
        ini_yes(ini, "PENALTY AFTER EXTRA TIME") ? "true" : "false");
    {
        const int32_t mins = atoi(ini_get(ini, "GAME LENGTH", v, sizeof v));
        put(o, " \"length\": %d,\n", mins >= 10 ? 3 : mins >= 7 ? 2 : mins >= 5 ? 1 : 0);
    }
    put(o, "    \"playing_up\": %d, \"kick_off\": %d, \"subs\": [%d, %d],", (int)playingUp, (int)kickOff,
        (int)ini_num(ini, "MIN SUBS", 0), (int)ini_num(ini, "MAX SUBS", 0));
    {
        int32_t s1[11], s2[11];
        const int32_t n1 = numbers(ini_get(ini, "TEAM1 SENT OFF PLAYERS", v, sizeof v), s1, 11);
        const int32_t n2 = numbers(ini_get(ini, "TEAM2 SENT OFF PLAYERS", v, sizeof v), s2, 11);
        put(o, " \"sent_off\": [[");
        for (int32_t k = 0; k < n1; k++)
            put(o, "%s%d", k ? ", " : "", (int)s1[k]);
        put(o, "], [");
        for (int32_t k = 0; k < n2; k++)
            put(o, "%s%d", k ? ", " : "", (int)s2[k]);
        put(o, "]],\n");
    }
    put(o, "    \"injured\": [");
    for (int32_t t = 1; t <= 2; t++) {
        int32_t shirts[8], levels[8];
        char key[48];
        snprintf(key, sizeof key, "TEAM%d INJURY PLAYERS", (int)t);
        const int32_t n = numbers(ini_get(ini, key, v, sizeof v), shirts, 8);
        snprintf(key, sizeof key, "TEAM%d INJURY PLAYERS LEVELS", (int)t);
        const int32_t nl = numbers(ini_get(ini, key, v, sizeof v), levels, 8);
        put(o, "%s[", t > 1 ? ", " : "");
        for (int32_t k = 0; k < n; k++)
            put(o, "%s[%d, %d]", k ? ", " : "", (int)shirts[k], (int)(k < nl ? levels[k] : 1));
        put(o, "]");
    }
    put(o, "],");
    if (ini_yes(ini, "SECOND LEG"))
        put(o, " \"first_leg\": [%d, %d],", (int)ini_num(ini, "TEAM1 FIRST LEG SCORE", 0),
            (int)ini_num(ini, "TEAM2 FIRST LEG SCORE", 0));
    {
        static const char *const pitches[7] = { "FROZEN", "MUDDY", "WET", "SOFT", "NORMAL", "DRY", "HARD" };
        ini_get(ini, "PITCH TYPE", v, sizeof v);
        for (int32_t k = 0; k < 7; k++)
            if (!strcmp(v, pitches[k]))
                put(o, " \"pitch_type\": %d,", (int)k);
    }
    /* the win */
    {
        char win[48];
        ini_get(ini, "WIN CONDITION", win, sizeof win);
        if (!strcmp(win, "AT LEAST DRAW")) {
            put(o, " \"win\": \"draw\",");
        } else if (!strcmp(win, "CUSTOM")) {
            const int32_t a = ini_num(ini, "WIN SCORE1", 0), b = ini_num(ini, "WIN SCORE2", 0);
            put(o, " \"win\": \"exact\", \"win_score\": [%d, %d],", (int)(yours == 0 ? a : b), (int)(yours == 0 ? b : a));
        } else {
            put(o, " \"win\": \"win\",");
            if (!strcmp(win, "GOLDEN GOAL"))
                put(o, " \"extra_time_rule\": \"golden_goal\",");
            else if (!strcmp(win, "SILVER GOAL"))
                put(o, " \"extra_time_rule\": \"silver_goal\",");
        }
    }
    /* the start */
    {
        char ev[64], over[96];
        int32_t shootout = !strncmp(ini_get(ini, "OVERRIDE EXTRA TIME OPTION", over, sizeof over), "IGNORE RELATED OPTIONS", 22);
        ini_get(ini, "STARTING EVENT", ev, sizeof ev);
        const char *kind = NULL;
        int32_t spot = 0, deep = strstr(ev, " HOME") != NULL, wing = -2;
        if (!strcmp(ev, "PENALTY SHOOTOUT")) {
            shootout = 1;
        } else if (!strncmp(ev, "RANDOM FREE KICK", 16)) {
            kind = "free_kick";
            spot = -1;
        } else if (!strncmp(ev, "LEFT FREE KICK ", 15) || !strncmp(ev, "RIGHT FREE KICK ", 16) ||
                   !strncmp(ev, "CENTER FREE KICK", 16)) {
            kind = "free_kick";
            const char *n = strstr(ev, "KICK ");      /* ("LEFT FREE KICK 2 HOME": 2) */
            spot = ev[0] == 'C' ? 4 : (ev[0] == 'L' ? 0 : 4) + (n ? atoi(n + 5) : 1);
        } else if (!strcmp(ev, "CUSTOM FREE KICK")) {
            kind = "free_kick";
        } else if (!strcmp(ev, "PENALTY KICK")) {
            kind = "penalty";
        } else if (!strcmp(ev, "RANDOM")) {
            kind = "random";                       /* (drawn at PLAY) */
        } else if (strstr(ev, "CORNER KICK") || strstr(ev, "GOAL KICK") || strstr(ev, "THROW IN")) {
            kind = strstr(ev, "CORNER") ? "corner" : strstr(ev, "GOAL") ? "goal_kick" : "throw_in";
            if (!strncmp(ev, "RANDOM", 6) || kickOff < 0 || playingUp < 0) {
                wing = -1;
            } else {    /* the screen's side as the taker's wing (his left the screen's attacking the top goal) */
                const int32_t attacksTop = (kickOff == 0) == (playingUp == 0);
                wing = ((ev[0] == 'L') == attacksTop) ? 0 : 1;
            }
        }
        if (shootout)
            put(o, " \"shootout\": true,");
        if (kind) {
            put(o, "\n    \"restart\": { \"kind\": \"%s\", \"team\": %d", kind, (int)kickOff);
            if (wing >= -1)
                put(o, ", \"wing\": %d", (int)wing);
            if (spot)
                put(o, ", \"spot\": %d", (int)spot);
            if (deep)
                put(o, ", \"deep\": true");
            if (!strcmp(ev, "CUSTOM FREE KICK"))
                put(o, ", \"x\": %d, \"y\": %d", (int)ini_num(ini, "CUSTOM FREE KICK X", 336),
                    (int)ini_num(ini, "CUSTOM FREE KICK Y", 449));
            put(o, " },");
        }
    }
    put(o, " \"locked\": false }");
    return 1;
}

/* ---- the export: challenge.ini as SWOS 2020's customWriteData, a 7z of it and the teams ---- */

/* the shirts of a bit mask / a list, "9,10" */
static void shirts_of(uint32_t mask, char *out, uint32_t cap)
{
    uint32_t n = 0;
    out[0] = 0;
    for (int32_t s = 0; s < 32 && n < cap; s++)
        if (mask & (1u << s))
            n += (uint32_t)snprintf(out + n, cap - n, "%s%d", n ? "," : "", (int)s);
}

static int32_t scn_ini(const ChallengeDef *c, char teamNames[2][24], const char *name, char *out, uint32_t cap)
{
    static const char *const pitches[7] = { "FROZEN", "MUDDY", "WET", "SOFT", "NORMAL", "DRY", "HARD" };
    static const char *const lengths[4] = { "3 MINS", "5 MINS", "7 MINS", "10 MINS" };
    const int32_t y = c->yours == 1;
    char ev[48] = "KICK OFF";
    int32_t starter = c->restartKind != CHALLENGE_RESTART_KICK_OFF && c->restartTeam >= 0 ? c->restartTeam : c->kickOff;
    Out o = { out, cap };
    if (c->shootoutOnly) {
        snprintf(ev, sizeof ev, "PENALTY SHOOTOUT");
    } else if (c->restartKind == CHALLENGE_RESTART_FREE_KICK) {
        const char *home = c->restartDeep ? " HOME" : "";
        if (c->restartSpot < 0)
            snprintf(ev, sizeof ev, "RANDOM FREE KICK%s", home);
        else if (c->restartSpot == 4)
            snprintf(ev, sizeof ev, "CENTER FREE KICK%s", home);
        else if (c->restartSpot > 0 && c->restartSpot < 8)
            snprintf(ev, sizeof ev, "%s FREE KICK %d%s", c->restartSpot < 4 ? "LEFT" : "RIGHT",
                     c->restartSpot < 4 ? c->restartSpot : c->restartSpot - 4, home);
        else
            snprintf(ev, sizeof ev, "CUSTOM FREE KICK");
    } else if (c->restartKind == CHALLENGE_RESTART_PENALTY) {
        snprintf(ev, sizeof ev, "PENALTY KICK");
    } else if (c->restartKind == CHALLENGE_RESTART_CORNER || c->restartKind == CHALLENGE_RESTART_THROW_IN ||
               c->restartKind == CHALLENGE_RESTART_GOAL_KICK) {
        const char *kind = c->restartKind == CHALLENGE_RESTART_CORNER ? "CORNER KICK"
                         : c->restartKind == CHALLENGE_RESTART_THROW_IN ? "THROW IN" : "GOAL KICK";
        const char *side = "RANDOM";
        if (c->restartWing >= 0 && c->kickOff >= 0 && c->playingUp >= 0)   /* (the import's rule) */
            side = (c->restartWing == 0) == ((c->kickOff == 0) == (c->playingUp == 0)) ? "LEFT" : "RIGHT";
        snprintf(ev, sizeof ev, "%s %s", side, kind);
    } else if (c->restartKind == CHALLENGE_RESTART_RANDOM) {
        snprintf(ev, sizeof ev, "RANDOM");
    }
#define KEY(k, ...) do { put(&o, "%s = ", k); put(&o, __VA_ARGS__); put(&o, "\r\n"); } while (0)
#define YES(v) ((v) ? "YES" : "NO")
    put(&o, "[%s]\r\n", name);
    KEY("CATEGORY", "%s", "CUSTOM");
    KEY("SCENARIO NAME", "%s", name);
    KEY("SCENARIO NICKNAME", "%s", c->title);
    KEY("TEAM FILE NAME", "%s", "");
    KEY("TEAM1 NAME", "%s", teamNames[0]);
    KEY("TEAM2 NAME", "%s", teamNames[1]);
    KEY("CPU TEAM", "%s", y ? "TEAM1" : "TEAM2");
    KEY("TEAM1 SCORE", "%d", c->score[0]);
    KEY("TEAM2 SCORE", "%d", c->score[1]);
    KEY("INITIAL TIME", "%d", c->minute);
    KEY("SECOND LEG", "%s", YES(c->secondLeg));
    KEY("TEAM1 FIRST LEG SCORE", "%d", c->firstLeg[0]);
    KEY("TEAM2 FIRST LEG SCORE", "%d", c->firstLeg[1]);
    KEY("EXTRA TIME", "%s", YES(c->extraTime));
    KEY("PENALTY AFTER EXTRA TIME", "%s", YES(c->penalties));
    KEY("OVERRIDE EXTRA TIME OPTION", "%s",
        c->shootoutOnly ? "IGNORE RELATED OPTIONS AND START PENALTY SHOOTOUT" : "");
    KEY("MIN SUBS", "%d", c->subsSet ? c->subsMin : 3);
    KEY("MAX SUBS", "%d", c->subsSet ? c->subsMax : 5);
    for (int32_t t = 0; t < 2; t++) {
        char key[48], list[96], levels[48];
        uint32_t n = 0, m = 0;
        shirts_of(c->sentOff[t], list, sizeof list);
        snprintf(key, sizeof key, "TEAM%d SENT OFF PLAYERS", (int)t + 1);
        KEY(key, "%s", list);
        list[0] = levels[0] = 0;
        for (int32_t k = 0; k < 8; k++)
            if (c->injured[t][k].shirt && n < sizeof list && m < sizeof levels) {
                n += (uint32_t)snprintf(list + n, sizeof list - n, "%s%d", n ? "," : "", c->injured[t][k].shirt);
                m += (uint32_t)snprintf(levels + m, sizeof levels - m, "%s%d", m ? "," : "", c->injured[t][k].level);
            }
        snprintf(key, sizeof key, "TEAM%d INJURY PLAYERS", (int)t + 1);
        KEY(key, "%s", list);
        snprintf(key, sizeof key, "TEAM%d INJURY PLAYERS LEVELS", (int)t + 1);
        KEY(key, "%s", levels);
    }
    KEY("CPU INJURY SPEED", "%s", "YES");
    KEY("TEAM PLAYING UP", "%s", c->playingUp == 0 ? "UP" : c->playingUp == 1 ? "DOWN" : "RANDOM");
    KEY("TEAM STARTING", "%s", starter == 0 ? "TEAM1" : starter == 1 ? "TEAM2" : "RANDOM");
    KEY("STARTING EVENT", "%s", ev);
    KEY("WIN CONDITION", "%s",
        c->win == CHALLENGE_EXACT ? "CUSTOM" : c->win == CHALLENGE_DRAW ? "AT LEAST DRAW"
        : c->extraTimeRule == CHALLENGE_ET_GOLDEN_GOAL ? "GOLDEN GOAL"
        : c->extraTimeRule == CHALLENGE_ET_SILVER_GOAL ? "SILVER GOAL" : "WIN BY ANY SCORE");
    KEY("DIFFICULTY", "%d", c->difficulty < 1 ? 1 : c->difficulty > 5 ? 5 : c->difficulty);
    KEY("WIN MESSAGE", "%s", c->winMessage);
    KEY("LOCKED", "%s", "");
    KEY("GAME LENGTH", "%s", lengths[c->length < 4 ? c->length : 0]);
    KEY("DIRECT RED CARD", "%s", "NO");
    KEY("CPU KEEPER LEVEL", "%s", "1");
    KEY("CUSTOM FREE KICK X", "%d", c->restartX);
    KEY("CUSTOM FREE KICK Y", "%d", c->restartY);
    KEY("CAMERA STARTING", "%s", "RANDOM");
    KEY("WIN SCORE1", "%d", c->win == CHALLENGE_EXACT ? c->winScore[y ? 1 : 0] : 0);
    KEY("WIN SCORE2", "%d", c->win == CHALLENGE_EXACT ? c->winScore[y ? 0 : 1] : 0);
    for (int32_t k = 0; k < 5; k++) {
        char key[16];
        snprintf(key, sizeof key, "DESCRIPTION%d", (int)k + 1);
        KEY(key, "%s", c->lines[k]);
    }
    KEY("PITCH TYPE", "%s", c->pitchType >= 0 && c->pitchType < 7 ? pitches[c->pitchType] : "RANDOM");
    KEY("PITCH SET", "%s", "GENERIC1");
#undef KEY
#undef YES
    return o.left > 1;
}

/* 7z's numbers: the first byte's high bits say how many bytes follow */
static void num7z(uint8_t **p, uint64_t v)
{
    uint8_t first = 0, mask = 0x80;
    int32_t i;
    for (i = 0; i < 8; i++) {
        if (v < ((uint64_t)1 << (7 * (i + 1)))) {
            first |= (uint8_t)(v >> (8 * i));
            break;
        }
        first |= mask;
        mask >>= 1;
    }
    *(*p)++ = first;
    for (; i > 0; i--) {
        *(*p)++ = (uint8_t)v;
        v >>= 8;
    }
}

static void le_bytes(uint8_t *p, uint64_t v, int32_t n)
{
    while (n--) {
        *p++ = (uint8_t)v;
        v >>= 8;
    }
}

typedef struct { const char *name; const uint8_t *data; uint32_t size; } Packed;

/* a 7z archive of the files, each stored as it is (the Copy coder) in a folder of its own */
static uint8_t *pack_stored(const Packed *files, int32_t count, uint32_t *outSize)
{
    static int crcReady;
    static uint8_t head[2048], names[1024];
    uint32_t data = 0, nameLen = 0;
    uint8_t *h = head;
    if (!crcReady) {
        CrcGenerateTable();
        crcReady = 1;
    }
    for (int32_t i = 0; i < count; i++) {
        data += files[i].size;
        for (const char *c = files[i].name; ; c++) {   /* (UTF-16LE, NUL-ended; the names ASCII) */
            if (nameLen + 2 > sizeof names)
                return NULL;
            names[nameLen++] = (uint8_t)*c;
            names[nameLen++] = 0;
            if (!*c)
                break;
        }
    }
    *h++ = 0x01;                            /* header */
    *h++ = 0x04;                            /* main streams */
    *h++ = 0x06;                            /* pack info: at 0, `count` streams, their sizes */
    num7z(&h, 0);
    num7z(&h, (uint64_t)count);
    *h++ = 0x09;
    for (int32_t i = 0; i < count; i++)
        num7z(&h, files[i].size);
    *h++ = 0x00;
    *h++ = 0x07;                            /* unpack info: a folder a file, one Copy coder */
    *h++ = 0x0B;
    num7z(&h, (uint64_t)count);
    *h++ = 0x00;
    for (int32_t i = 0; i < count; i++) {
        *h++ = 0x01;                        /* one coder */
        *h++ = 0x01;                        /* its id one byte long: */
        *h++ = 0x00;                        /* Copy */
    }
    *h++ = 0x0C;                            /* unpacked sizes */
    for (int32_t i = 0; i < count; i++)
        num7z(&h, files[i].size);
    *h++ = 0x0A;                            /* CRCs, all there */
    *h++ = 0x01;
    for (int32_t i = 0; i < count; i++) {
        le_bytes(h, CrcCalc(files[i].data, files[i].size), 4);
        h += 4;
    }
    *h++ = 0x00;                            /* (unpack info) */
    *h++ = 0x00;                            /* (main streams) */
    *h++ = 0x05;                            /* files */
    num7z(&h, (uint64_t)count);
    *h++ = 0x11;                            /* their names */
    num7z(&h, (uint64_t)nameLen + 1);
    *h++ = 0x00;
    if (nameLen + 8 > sizeof head - (size_t)(h - head))
        return NULL;
    memcpy(h, names, nameLen);
    h += nameLen;
    *h++ = 0x00;                            /* (files) */
    *h++ = 0x00;                            /* (header) */

    const uint32_t headLen = (uint32_t)(h - head), total = 32 + data + headLen;
    uint8_t *out = (uint8_t *)malloc(total);
    if (!out)
        return NULL;
    static const uint8_t magic[8] = { '7', 'z', 0xBC, 0xAF, 0x27, 0x1C, 0, 4 };
    memcpy(out, magic, sizeof magic);
    le_bytes(out + 12, data, 8);            /* where the header is, past these 32 bytes */
    le_bytes(out + 20, headLen, 8);
    le_bytes(out + 28, CrcCalc(head, headLen), 4);
    le_bytes(out + 8, CrcCalc(out + 12, 20), 4);
    uint32_t at = 32;
    for (int32_t i = 0; i < count; i++) {
        memcpy(out + at, files[i].data, files[i].size);
        at += files[i].size;
    }
    memcpy(out + at, head, headLen);
    *outSize = total;
    return out;
}

int32_t UnitedScn_Export(const ChallengeDef *c, const TeamFileEx teams[2], const char *name, unsigned char **out,
                         uint32_t *outSize)
{
    static char ini[8192];
    static uint8_t recs[2][kTeamRecord];
    char teamNames[2][24], files[2][40];
    Packed packed[3];
    int32_t count = 0;
    *out = NULL;
    *outSize = 0;
    for (int32_t t = 0; t < 2; t++) {
        memcpy(recs[t], &teams[t], kTeamRecord);
        const uint16_t global = (uint16_t)teams[t].team.globalTeamNumber;   /* (.TEAM: little-endian) */
        recs[t][2] = (uint8_t)global;
        recs[t][3] = (uint8_t)(global >> 8);
        record_name(recs[t], teamNames[t], sizeof teamNames[t]);
        if (!teamNames[t][0])
            return 0;
        snprintf(files[t], sizeof files[t], "%s.TEAM", teamNames[t]);
        for (char *p = files[t]; *p; p++)
            if (*p == '/' || *p == '\\')
                *p = '_';
    }
    if (!scn_ini(c, teamNames, name, ini, sizeof ini))
        return 0;
    packed[count++] = (Packed){ "challenge.ini", (const uint8_t *)ini, (uint32_t)strlen(ini) };
    for (int32_t t = 0; t < 2; t++)
        if (!t || strcmp(files[0], files[1]) != 0)      /* (a team against itself: one file) */
            packed[count++] = (Packed){ files[t], recs[t], kTeamRecord };
    *out = pack_stored(packed, count, outSize);
    return *out != NULL;
}

/* ---- the import ---- */

int32_t UnitedScn_Import(const unsigned char *data, uint32_t size, char *id, uint32_t idCap)
{
    Scn scn;
    char edt[48], names[2][24], v[64];
    if (!unpack(data, size, &scn)) {
        fprintf(stderr, "[scn] not a 7z archive SWOS 2020 would open\n");
        return 0;
    }
    /* the teams: TEAMn NAME's record (a .TEAM of that name), else the archive's first two */
    int32_t pick[2] = { -1, -1 };
    for (int32_t t = 0; t < 2; t++) {
        char key[16];
        snprintf(key, sizeof key, "TEAM%d NAME", (int)t + 1);
        ini_get(scn.ini, key, v, sizeof v);
        for (int32_t k = 0; k < scn.numTeams && pick[t] < 0; k++) {
            char rn[24];
            record_name(scn.teams[k].rec, rn, sizeof rn);
            if (same_team(rn, v) && k != pick[0])
                pick[t] = k;
        }
    }
    for (int32_t t = 0; t < 2; t++)
        if (pick[t] < 0)
            pick[t] = scn.numTeams == 1 ? 0        /* (a team against itself) */
                    : t < scn.numTeams && t != pick[1 - t] ? t : (1 - t < scn.numTeams ? 1 - t : -1);
    if (pick[0] < 0 || pick[1] < 0) {
        fprintf(stderr, "[scn] the archive has not two teams\n");
        free(scn.ini);
        return 0;
    }
    /* the .EDT: a big-endian count, then the records, each its 16-bit field big-endian */
    {
        unsigned char buf[2 + 2 * kTeamRecord];
        buf[0] = 0;
        buf[1] = 2;
        for (int32_t t = 0; t < 2; t++) {
            unsigned char *r = buf + 2 + t * kTeamRecord;
            memcpy(r, scn.teams[pick[t]].rec, kTeamRecord);
            const unsigned char lo = r[2];
            r[2] = r[3];
            r[3] = lo;
            record_name(r, names[t], sizeof names[t]);
        }
        char sname[96];
        ini_get(scn.ini, "SCENARIO NAME", sname, sizeof sname);
        snprintf(edt, sizeof edt, "scn-%08x.EDT", (unsigned)fnv32(sname));
        if (ModApi_SaveFile("challenges", edt, buf, sizeof buf) != 0) {
            free(scn.ini);
            return 0;
        }
    }
    /* shared.json: the challenges already there (but one of this id), then this one */
    {
        static char obj[8192];
        Out o = { obj, sizeof obj };
        if (!challenge_json(&scn, edt, (const char (*)[24])names, id, idCap, &o)) {
            free(scn.ini);
            return 0;
        }
        free(scn.ini);
        uint32_t oldSize = 0;
        char *old = (char *)ModApi_LoadFile("challenges", "shared.json", &oldSize);
        const uint32_t cap = oldSize + sizeof obj + 256;
        char *buf = (char *)malloc(cap);
        if (!buf) {
            free(old);
            return 0;
        }
        Out w = { buf, cap };
        put(&w, "{\n \"format\": \"openswos-challenges\",\n \"version\": 2,\n \"category\": \"CUSTOM\",\n"
                " \"about\": \"SWOS 2020's shared custom challenges, fetched (SWOS United)\",\n \"challenges\": [\n");
        int32_t count = 0;
        const char *arr;
        int arrLen;
        if (old && mjson_find(old, (int)oldSize, "$.challenges", &arr, &arrLen) == MJSON_TOK_ARRAY) {
            int koff, klen, voff, vlen, vtype, off = 0;
            int32_t drop = -(kFetchedKept - 1);     /* (the oldest go, past kFetchedKept) */
            while ((off = mjson_next(arr, arrLen, off, &koff, &klen, &voff, &vlen, &vtype)) != 0) {
                char oid[48] = "";
                if (vtype == MJSON_TOK_OBJECT) {
                    mjson_get_string(arr + voff, vlen, "$.id", oid, (int)sizeof oid);
                    drop += strcmp(oid, id) != 0;
                }
            }
            off = 0;
            while ((off = mjson_next(arr, arrLen, off, &koff, &klen, &voff, &vlen, &vtype)) != 0) {
                char oid[48] = "";
                if (vtype != MJSON_TOK_OBJECT)
                    continue;
                mjson_get_string(arr + voff, vlen, "$.id", oid, (int)sizeof oid);
                if (!strcmp(oid, id))
                    continue;
                if (drop > 0) {
                    drop--;
                    continue;
                }
                put(&w, "%s%.*s", count++ ? ",\n" : "", vlen, arr + voff);
            }
        }
        put(&w, "%s%s\n ]\n}\n", count ? ",\n" : "", obj);
        free(old);
        const int32_t ok = ModApi_SaveFile("challenges", "shared.json", buf, (uint32_t)(w.p - buf)) == 0;
        free(buf);
        if (!ok)
            return 0;
    }
    fprintf(stderr, "[scn] imported %s\n", id);
    return 1;
}

#endif
